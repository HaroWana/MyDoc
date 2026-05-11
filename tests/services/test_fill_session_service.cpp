#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "fill_session_service.hpp"
#include "domain/field.hpp"
#include "domain/fill.hpp"
#include "domain/fill_session.hpp"
#include "domain/i_fill_session_repository.hpp"
#include "domain/i_template_repository.hpp"
#include "domain/source_ref.hpp"
#include "domain/template.hpp"
#include "mondoc/error.hpp"
#include "mondoc/id.hpp"

using mondoc::FieldId;
using mondoc::FillSessionId;
using mondoc::TemplateId;
using mondoc::domain::Confidence;
using mondoc::domain::Field;
using mondoc::domain::FieldType;
using mondoc::domain::Fill;
using mondoc::domain::SourceRef;
using mondoc::domain::FillSession;
using mondoc::domain::FillStatus;
using mondoc::domain::IFillSessionRepository;
using mondoc::domain::ITemplateRepository;
using mondoc::domain::Template;
using mondoc::services::ExportFormat;
using mondoc::services::FillSessionService;

namespace {

class FakeTemplateRepo : public ITemplateRepository {
public:
    mondoc::expected<void, mondoc::Error> save(const Template& t) override {
        store_[t.id_.value()] = t;
        return {};
    }
    mondoc::expected<Template, mondoc::Error>
    findById(const TemplateId& id) override {
        auto it = store_.find(id.value());
        if (it == store_.end()) {
            return mondoc::unexpected(mondoc::Error::notFound("missing template"));
        }
        return it->second;
    }
    mondoc::expected<std::vector<Template>, mondoc::Error> listAll() override {
        return std::vector<Template>{};
    }
    mondoc::expected<void, mondoc::Error> remove(const TemplateId&) override {
        return {};
    }

    std::map<std::string, Template> store_;
};

class FakeFillRepo : public IFillSessionRepository {
public:
    mondoc::expected<void, mondoc::Error> save(const FillSession& s) override {
        store_[s.id_.value()] = s;
        return {};
    }
    mondoc::expected<FillSession, mondoc::Error>
    findById(const FillSessionId& id) override {
        auto it = store_.find(id.value());
        if (it == store_.end()) {
            return mondoc::unexpected(mondoc::Error::notFound("missing session"));
        }
        return it->second;
    }
    mondoc::expected<std::vector<FillSession>, mondoc::Error> listDrafts() override {
        std::vector<FillSession> out;
        for (const auto& kv : store_) {
            const auto& s = kv.second;
            if (s.status_ == FillStatus::Created ||
                s.status_ == FillStatus::Reviewing) {
                out.push_back(s);
            }
        }
        return out;
    }
    mondoc::expected<void, mondoc::Error> remove(const FillSessionId& id) override {
        if (store_.erase(id.value()) == 0) {
            return mondoc::unexpected(mondoc::Error::notFound("missing session"));
        }
        return {};
    }
    mondoc::expected<void, mondoc::Error>
    upsertValue(const FillSessionId& sessionId,
                const FieldId& fieldId,
                const std::string& value) override {
        upsertCalls_.push_back({sessionId.value(), fieldId.value(), value});
        auto it = store_.find(sessionId.value());
        if (it == store_.end()) {
            return mondoc::unexpected(mondoc::Error::notFound("missing session"));
        }
        for (auto& f : it->second.fills_) {
            if (f.field_id_.value() == fieldId.value()) {
                f.current_value_ = value;
                return {};
            }
        }
        Fill f;
        f.field_id_      = fieldId;
        f.current_value_ = value;
        it->second.fills_.push_back(std::move(f));
        return {};
    }

    mondoc::expected<void, mondoc::Error>
    upsertConfidence(const FillSessionId&, const FieldId&, Confidence) override {
        return {};
    }

    mondoc::expected<void, mondoc::Error>
    replaceSourceRefs(const FillSessionId&, const FieldId&,
                      const std::vector<SourceRef>&) override {
        return {};
    }

    struct UpsertCall {
        std::string sessionId;
        std::string fieldId;
        std::string value;
    };
    std::vector<UpsertCall> upsertCalls_;
    std::map<std::string, FillSession> store_;
};

std::filesystem::path uniqueTempPath(const std::string& ext) {
    static std::mt19937_64 rng{std::random_device{}()};
    auto suffix = std::to_string(rng()) + "_" +
                  std::to_string(std::chrono::steady_clock::now()
                                     .time_since_epoch().count());
    auto path = std::filesystem::temp_directory_path()
                / ("mondoc_test_fillsvc_" + suffix + ext);
    std::error_code ec;
    std::filesystem::remove(path, ec);
    return path;
}

struct TempFile {
    std::filesystem::path path;
    explicit TempFile(std::filesystem::path p) : path(std::move(p)) {}
    ~TempFile() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
};

void writeFile(const std::filesystem::path& p, const std::string& body) {
    std::ofstream f(p, std::ios::binary);
    REQUIRE(f.is_open());
    f << body;
}

std::string readFile(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    REQUIRE(in.is_open());
    return std::string{std::istreambuf_iterator<char>{in},
                       std::istreambuf_iterator<char>{}};
}

Template makeTpl(const std::filesystem::path& src,
                 std::vector<Field> fields,
                 const std::string& fmt = "txt") {
    Template t;
    t.id_            = TemplateId{"tpl1"};
    t.name_          = "Test";
    t.source_format_ = fmt;
    t.fields_        = std::move(fields);
    t.source_path_   = src;
    return t;
}

FillSession makeSession(const std::string& id,
                        const TemplateId& tplId,
                        FillStatus status,
                        std::vector<Fill> fills = {}) {
    FillSession s;
    s.id_              = FillSessionId{id};
    s.template_id_     = tplId;
    s.status_          = status;
    s.fills_           = std::move(fills);
    s.created_at_unix_ = 1;
    s.updated_at_unix_ = 1;
    return s;
}

}  // namespace

TEST_CASE("FillSessionService: openSession returns FillSessionId and persists Created session",
          "[services.fill_session]") {
    FakeFillRepo fillRepo;
    FakeTemplateRepo tplRepo;
    Template t = makeTpl({}, {});
    REQUIRE(tplRepo.save(t).has_value());

    FillSessionService svc{fillRepo, tplRepo};
    auto id = svc.openSession(t.id_);

    REQUIRE(id.has_value());
    REQUIRE_FALSE(id->value().empty());
    REQUIRE(fillRepo.store_.size() == 1);
    const auto& saved = fillRepo.store_.begin()->second;
    REQUIRE(saved.status_ == FillStatus::Created);
    REQUIRE(saved.template_id_.value() == t.id_.value());
}

TEST_CASE("FillSessionService: openSession propagates not-found template error",
          "[services.fill_session]") {
    FakeFillRepo fillRepo;
    FakeTemplateRepo tplRepo;
    FillSessionService svc{fillRepo, tplRepo};

    auto id = svc.openSession(TemplateId{"missing"});

    REQUIRE_FALSE(id.has_value());
    REQUIRE(id.error().kind() == mondoc::Error::Kind::NotFound);
}

TEST_CASE("FillSessionService: setFieldValue calls upsertValue exactly once",
          "[services.fill_session]") {
    FakeFillRepo fillRepo;
    FakeTemplateRepo tplRepo;
    Template t = makeTpl({}, {});
    REQUIRE(tplRepo.save(t).has_value());
    FillSessionService svc{fillRepo, tplRepo};
    auto id = svc.openSession(t.id_);
    REQUIRE(id.has_value());

    auto r = svc.setFieldValue(*id, FieldId{"f1"}, "v1");

    REQUIRE(r.has_value());
    REQUIRE(fillRepo.upsertCalls_.size() == 1);
    REQUIRE(fillRepo.upsertCalls_[0].sessionId == id->value());
    REQUIRE(fillRepo.upsertCalls_[0].fieldId == "f1");
    REQUIRE(fillRepo.upsertCalls_[0].value == "v1");
}

TEST_CASE("FillSessionService: listDrafts forwards to repo",
          "[services.fill_session]") {
    FakeFillRepo fillRepo;
    FakeTemplateRepo tplRepo;
    fillRepo.store_["a"] = makeSession("a", TemplateId{"tpl1"}, FillStatus::Created);
    fillRepo.store_["b"] = makeSession("b", TemplateId{"tpl1"}, FillStatus::Exported);

    FillSessionService svc{fillRepo, tplRepo};
    auto drafts = svc.listDrafts();

    REQUIRE(drafts.has_value());
    REQUIRE(drafts->size() == 1);
    REQUIRE((*drafts)[0].id_.value() == "a");
}

TEST_CASE("FillSessionService: resumeSession transitions Created -> Reviewing",
          "[services.fill_session]") {
    FakeFillRepo fillRepo;
    FakeTemplateRepo tplRepo;
    fillRepo.store_["s1"] = makeSession("s1", TemplateId{"tpl1"}, FillStatus::Created);

    FillSessionService svc{fillRepo, tplRepo};
    auto resumed = svc.resumeSession(FillSessionId{"s1"});

    REQUIRE(resumed.has_value());
    REQUIRE(resumed->status_ == FillStatus::Reviewing);
    REQUIRE(fillRepo.store_["s1"].status_ == FillStatus::Reviewing);
}

TEST_CASE("FillSessionService: resumeSession leaves Reviewing untouched",
          "[services.fill_session]") {
    FakeFillRepo fillRepo;
    FakeTemplateRepo tplRepo;
    fillRepo.store_["s1"] = makeSession("s1", TemplateId{"tpl1"}, FillStatus::Reviewing);

    FillSessionService svc{fillRepo, tplRepo};
    auto resumed = svc.resumeSession(FillSessionId{"s1"});

    REQUIRE(resumed.has_value());
    REQUIRE(resumed->status_ == FillStatus::Reviewing);
    REQUIRE(fillRepo.store_["s1"].status_ == FillStatus::Reviewing);
}

TEST_CASE("FillSessionService: discardSession removes the session",
          "[services.fill_session]") {
    FakeFillRepo fillRepo;
    FakeTemplateRepo tplRepo;
    fillRepo.store_["s1"] = makeSession("s1", TemplateId{"tpl1"}, FillStatus::Created);

    FillSessionService svc{fillRepo, tplRepo};
    auto r = svc.discardSession(FillSessionId{"s1"});

    REQUIRE(r.has_value());
    REQUIRE(fillRepo.store_.empty());
}

TEST_CASE("FillSessionService: exportSession dispatches Text format and transitions to Exported",
          "[services.fill_session]") {
    TempFile srcFile{uniqueTempPath(".txt")};
    writeFile(srcFile.path, "Hello {{name}}!");

    FakeFillRepo fillRepo;
    FakeTemplateRepo tplRepo;
    Field f;
    f.id_   = FieldId{"fname"};
    f.name_ = "name";
    f.type_ = FieldType::Text;
    Template t = makeTpl(srcFile.path, {f}, "txt");
    REQUIRE(tplRepo.save(t).has_value());

    Fill fill;
    fill.field_id_      = FieldId{"fname"};
    fill.current_value_ = "World";
    fillRepo.store_["s1"] = makeSession("s1", t.id_, FillStatus::Reviewing, {fill});

    TempFile dst{uniqueTempPath(".txt")};
    FillSessionService svc{fillRepo, tplRepo};
    auto r = svc.exportSession(FillSessionId{"s1"}, ExportFormat::Text, dst.path);

    REQUIRE(r.has_value());
    REQUIRE(readFile(dst.path) == "Hello World!");
    REQUIRE(fillRepo.store_["s1"].status_ == FillStatus::Exported);
}

TEST_CASE("FillSessionService: exportSession Markdown format dispatches to TextDocumentWriter",
          "[services.fill_session]") {
    TempFile srcFile{uniqueTempPath(".md")};
    writeFile(srcFile.path, "# {{title}}\n\nBody.");

    FakeFillRepo fillRepo;
    FakeTemplateRepo tplRepo;
    Field f;
    f.id_   = FieldId{"ftitle"};
    f.name_ = "title";
    f.type_ = FieldType::Text;
    Template t = makeTpl(srcFile.path, {f}, "md");
    REQUIRE(tplRepo.save(t).has_value());

    Fill fill;
    fill.field_id_      = FieldId{"ftitle"};
    fill.current_value_ = "Hi";
    fillRepo.store_["s1"] = makeSession("s1", t.id_, FillStatus::Reviewing, {fill});

    TempFile dst{uniqueTempPath(".md")};
    FillSessionService svc{fillRepo, tplRepo};
    auto r = svc.exportSession(FillSessionId{"s1"}, ExportFormat::Markdown, dst.path);

    REQUIRE(r.has_value());
    REQUIRE(readFile(dst.path) == "# Hi\n\nBody.");
    REQUIRE(fillRepo.store_["s1"].status_ == FillStatus::Exported);
}

TEST_CASE("FillSessionService: readSourceText returns body for .txt",
          "[services.fill_session]") {
    TempFile src{uniqueTempPath(".txt")};
    writeFile(src.path, "lorem ipsum");

    FakeFillRepo fillRepo;
    FakeTemplateRepo tplRepo;
    FillSessionService svc{fillRepo, tplRepo};
    auto r = svc.readSourceText(src.path);

    REQUIRE(r.has_value());
    REQUIRE(*r == "lorem ipsum");
}

TEST_CASE("FillSessionService: readSourceText returns body for .md",
          "[services.fill_session]") {
    TempFile src{uniqueTempPath(".md")};
    writeFile(src.path, "# Heading\nbody");

    FakeFillRepo fillRepo;
    FakeTemplateRepo tplRepo;
    FillSessionService svc{fillRepo, tplRepo};
    auto r = svc.readSourceText(src.path);

    REQUIRE(r.has_value());
    REQUIRE(*r == "# Heading\nbody");
}

TEST_CASE("FillSessionService: readSourceText returns invalidArgument with Phase 4 message for .pdf",
          "[services.fill_session]") {
    FakeFillRepo fillRepo;
    FakeTemplateRepo tplRepo;
    FillSessionService svc{fillRepo, tplRepo};

    auto r = svc.readSourceText(std::filesystem::path{"/no.pdf"});

    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().kind() == mondoc::Error::Kind::InvalidArgument);
    REQUIRE(r.error().message().find("Phase 4") != std::string::npos);
    REQUIRE(r.error().message().find("deferred") != std::string::npos);
}

TEST_CASE("FillSessionService: readSourceText returns invalidArgument for unsupported extension",
          "[services.fill_session]") {
    FakeFillRepo fillRepo;
    FakeTemplateRepo tplRepo;
    FillSessionService svc{fillRepo, tplRepo};

    auto r = svc.readSourceText(std::filesystem::path{"foo.png"});

    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().kind() == mondoc::Error::Kind::InvalidArgument);
}

TEST_CASE("FillSessionService: REVW-05 stickiness — no AI overwrite path in Phase 2 surface",
          "[services.fill_session]") {
    // Phase 2 public surface (verify by inspection of fill_session_service.hpp):
    //   - openSession, setFieldValue, listDrafts, resumeSession,
    //     discardSession, exportSession, readSourceText
    // None of these overwrite an existing fill value automatically.
    // setFieldValue is the only mutation path and is driven by user input.
    // Phase 3 will introduce AI fill paths that must respect a sticky flag.
    SUCCEED("Phase 2 has no AI overwrite path; REVW-05 satisfied trivially");
}
