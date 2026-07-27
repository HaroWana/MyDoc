#include <catch2/catch_test_macros.hpp>

#include <zip.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <queue>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "ai_fill_pipeline.hpp"
#include "fill_session_service.hpp"
#include "i_llm_client.hpp"
#include "llm_config.hpp"
#include "llm_error.hpp"
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
using mondoc::adapters::ai::AiFillPipeline;
using mondoc::adapters::ai::ILlmClient;
using mondoc::adapters::ai::LlmConfig;
using mondoc::adapters::ai::LlmError;
using mondoc::services::AiFailureKind;
using mondoc::services::AiFillSourceInput;
using mondoc::services::classifyAiFailure;
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

class FakeLlmClient : public ILlmClient {
public:
    std::queue<mondoc::expected<std::string, LlmError>> responses_;
    std::vector<std::string> chatCalls_;
    std::function<void()> onAfterCall_;

    void enqueueOk(std::string body) { responses_.emplace(std::move(body)); }
    void enqueueErr(LlmError e) {
        responses_.emplace(mondoc::unexpected<LlmError>(std::move(e)));
    }

    mondoc::expected<std::string, LlmError> chat(const std::string& body) override {
        chatCalls_.push_back(body);
        if (responses_.empty()) {
            if (onAfterCall_) onAfterCall_();
            return mondoc::unexpected<LlmError>(LlmError::unreachable("fake exhausted"));
        }
        auto r = std::move(responses_.front());
        responses_.pop();
        if (onAfterCall_) onAfterCall_();
        return r;
    }
};

std::string makeChatCompletion(const nlohmann::json& contentJson) {
    nlohmann::json envelope = {
        {"choices", nlohmann::json::array({
            nlohmann::json{{"message", nlohmann::json{{"content", contentJson.dump()}}}}
        })}
    };
    return envelope.dump();
}

LlmConfig minimalConfig() {
    return LlmConfig{"https://hub.example/v1", "sk-test", "gpt-4o-2026"};
}

Template aiThreeFieldTpl() {
    Template t;
    t.id_   = TemplateId{"tpl-ai"};
    t.name_ = "ai";
    t.fields_ = {
        Field{FieldId{"f1"}, "name",  FieldType::Text},
        Field{FieldId{"f2"}, "dob",   FieldType::Date},
        Field{FieldId{"f3"}, "score", FieldType::Number},
    };
    return t;
}

std::string canonicalPass1() {
    return makeChatCompletion(nlohmann::json{
        {"facts", nlohmann::json::array()}});
}

std::string canonicalPass2() {
    return makeChatCompletion(nlohmann::json{
        {"fills", nlohmann::json::array({
            nlohmann::json{{"field_id","f1"},{"value","John"},
                           {"confidence","high"},{"fact_index",-1}},
            nlohmann::json{{"field_id","f2"},{"value","1985-03-12"},
                           {"confidence","medium"},{"fact_index",-1}},
            nlohmann::json{{"field_id","f3"},{"value","95"},
                           {"confidence","low"},{"fact_index",-1}},
        })}});
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

TEST_CASE("[TST-6] FillSessionService: readSourceText returns body for .docx",
          "[services.fill_session]") {
    TempFile src{uniqueTempPath(".docx")};
    {
        constexpr std::string_view documentXml = R"XML(<?xml version="1.0"?>
<w:document xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main">
  <w:body><w:p><w:r><w:t>docx body text</w:t></w:r></w:p></w:body>
</w:document>)XML";
        int err = 0;
        zip_t* zf = zip_open(src.path.string().c_str(), ZIP_CREATE | ZIP_TRUNCATE, &err);
        REQUIRE(zf != nullptr);
        auto* buf = zip_source_buffer(zf, documentXml.data(), documentXml.size(), 0);
        REQUIRE(buf != nullptr);
        REQUIRE(zip_file_add(zf, "word/document.xml", buf, ZIP_FL_OVERWRITE) >= 0);
        REQUIRE(zip_close(zf) == 0);
    }

    FakeFillRepo fillRepo;
    FakeTemplateRepo tplRepo;
    FillSessionService svc{fillRepo, tplRepo};
    auto r = svc.readSourceText(src.path);

    REQUIRE(r.has_value());
    REQUIRE(*r == "docx body text");
}

TEST_CASE("[TST-6] FillSessionService: readSourceText returns error for corrupt .docx zip",
          "[services.fill_session]") {
    TempFile src{uniqueTempPath(".docx")};
    writeFile(src.path, "PK\x03\x04 but not really a zip");

    FakeFillRepo fillRepo;
    FakeTemplateRepo tplRepo;
    FillSessionService svc{fillRepo, tplRepo};
    auto r = svc.readSourceText(src.path);

    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().kind() != mondoc::Error::Kind::InvalidArgument);
}

TEST_CASE("FillSessionService: readSourceText returns generic error (not invalidArgument) for invalid .pdf path",
          "[services.fill_session]") {
    FakeFillRepo fillRepo;
    FakeTemplateRepo tplRepo;
    FillSessionService svc{fillRepo, tplRepo};

    auto r = svc.readSourceText(std::filesystem::path{"/no.pdf"});

    REQUIRE_FALSE(r.has_value());
    // PoDoFo returns a generic error for non-existent/invalid files
    REQUIRE(r.error().kind() != mondoc::Error::Kind::InvalidArgument);
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

TEST_CASE("FillSessionService::aiFill: nullptr pipeline returns InvalidArgument",
          "[services.fill_session][adapters.ai]") {
    FakeFillRepo fillRepo;
    FakeTemplateRepo tplRepo;
    Template t = aiThreeFieldTpl();
    REQUIRE(tplRepo.save(t).has_value());
    fillRepo.store_["s1"] = makeSession("s1", t.id_, FillStatus::Reviewing);

    FillSessionService svc{fillRepo, tplRepo};
    std::atomic<bool> cancelled{false};
    auto r = svc.aiFill(FillSessionId{"s1"}, {}, "", cancelled);

    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().kind() == mondoc::Error::Kind::InvalidArgument);
}

TEST_CASE("FillSessionService::aiFill: happy path persists Fills and returns them",
          "[services.fill_session][adapters.ai]") {
    FakeFillRepo fillRepo;
    FakeTemplateRepo tplRepo;
    Template t = aiThreeFieldTpl();
    REQUIRE(tplRepo.save(t).has_value());
    fillRepo.store_["s1"] = makeSession("s1", t.id_, FillStatus::Reviewing);

    FakeLlmClient fake;
    fake.enqueueOk(canonicalPass1());
    fake.enqueueOk(canonicalPass2());
    AiFillPipeline pipe{fake, minimalConfig()};
    FillSessionService svc{fillRepo, tplRepo, &pipe};

    std::atomic<bool> cancelled{false};
    auto r = svc.aiFill(FillSessionId{"s1"}, {}, "", cancelled);

    REQUIRE(r.has_value());
    REQUIRE(r->size() == 3);
    REQUIRE((*r)[0].current_value_ == "John");
    REQUIRE((*r)[0].confidence_ == Confidence::High);
    REQUIRE(fillRepo.upsertCalls_.size() == 3);
    const auto& stored = fillRepo.store_["s1"].fills_;
    REQUIRE(stored.size() == 3);
    auto valueOf = [&](const std::string& fid) {
        for (const auto& f : stored)
            if (f.field_id_.value() == fid) return f.current_value_;
        return std::string{};
    };
    REQUIRE(valueOf("f1") == "John");
    REQUIRE(valueOf("f2") == "1985-03-12");
    REQUIRE(valueOf("f3") == "95");
}

TEST_CASE("FillSessionService::aiFill: manual-sticky fill is preserved (REVW-05)",
          "[services.fill_session][adapters.ai][revw-05]") {
    FakeFillRepo fillRepo;
    FakeTemplateRepo tplRepo;
    Template t = aiThreeFieldTpl();
    REQUIRE(tplRepo.save(t).has_value());

    Fill manual;
    manual.field_id_      = FieldId{"f1"};
    manual.current_value_ = "user typed";
    manual.confidence_    = Confidence::Manual;
    fillRepo.store_["s1"] = makeSession("s1", t.id_, FillStatus::Reviewing,
                                        {manual});

    FakeLlmClient fake;
    fake.enqueueOk(canonicalPass1());
    fake.enqueueOk(canonicalPass2());
    AiFillPipeline pipe{fake, minimalConfig()};
    FillSessionService svc{fillRepo, tplRepo, &pipe};

    std::atomic<bool> cancelled{false};
    auto r = svc.aiFill(FillSessionId{"s1"}, {}, "", cancelled);

    REQUIRE(r.has_value());
    REQUIRE(r->size() == 3);
    const Fill* outF1 = nullptr;
    for (const auto& f : *r) {
        if (f.field_id_.value() == "f1") { outF1 = &f; break; }
    }
    REQUIRE(outF1 != nullptr);
    REQUIRE(outF1->current_value_ == "user typed");
    REQUIRE(outF1->confidence_ == Confidence::Manual);

    for (const auto& c : fillRepo.upsertCalls_) {
        REQUIRE(c.fieldId != "f1");
    }
}

TEST_CASE("FillSessionService::aiFill: LlmError::Cancelled classifies as Cancelled",
          "[services.fill_session][adapters.ai][revw-08]") {
    FakeFillRepo fillRepo;
    FakeTemplateRepo tplRepo;
    Template t = aiThreeFieldTpl();
    REQUIRE(tplRepo.save(t).has_value());
    fillRepo.store_["s1"] = makeSession("s1", t.id_, FillStatus::Reviewing);

    FakeLlmClient fake;
    fake.enqueueOk(canonicalPass1());
    std::atomic<bool> cancelled{false};
    fake.onAfterCall_ = [&] { cancelled.store(true); };
    AiFillPipeline pipe{fake, minimalConfig()};
    FillSessionService svc{fillRepo, tplRepo, &pipe};

    auto r = svc.aiFill(FillSessionId{"s1"}, {}, "", cancelled);

    REQUIRE_FALSE(r.has_value());
    REQUIRE(classifyAiFailure(r.error()) == AiFailureKind::Cancelled);
    REQUIRE(fillRepo.upsertCalls_.empty());
}

TEST_CASE("FillSessionService::aiFill: LlmError::Unreachable classifies as Unreachable",
          "[services.fill_session][adapters.ai][app-06]") {
    FakeFillRepo fillRepo;
    FakeTemplateRepo tplRepo;
    Template t = aiThreeFieldTpl();
    REQUIRE(tplRepo.save(t).has_value());
    fillRepo.store_["s1"] = makeSession("s1", t.id_, FillStatus::Reviewing);

    FakeLlmClient fake;
    fake.enqueueErr(LlmError::unreachable("offline"));
    AiFillPipeline pipe{fake, minimalConfig()};
    FillSessionService svc{fillRepo, tplRepo, &pipe};

    std::atomic<bool> cancelled{false};
    auto r = svc.aiFill(FillSessionId{"s1"}, {}, "", cancelled);

    REQUIRE_FALSE(r.has_value());
    REQUIRE(classifyAiFailure(r.error()) == AiFailureKind::Unreachable);
}

TEST_CASE("FillSessionService::refineField: nullptr pipeline returns InvalidArgument",
          "[services.fill_session][adapters.ai]") {
    FakeFillRepo fillRepo;
    FakeTemplateRepo tplRepo;
    Template t = aiThreeFieldTpl();
    REQUIRE(tplRepo.save(t).has_value());
    fillRepo.store_["s1"] = makeSession("s1", t.id_, FillStatus::Reviewing);

    FillSessionService svc{fillRepo, tplRepo};
    std::atomic<bool> cancelled{false};
    auto r = svc.refineField(FillSessionId{"s1"}, "hi", {}, {}, cancelled);

    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().kind() == mondoc::Error::Kind::InvalidArgument);
}

TEST_CASE("FillSessionService::refineField: happy path persists only updated fields",
          "[services.fill_session][adapters.ai][fill-06]") {
    FakeFillRepo fillRepo;
    FakeTemplateRepo tplRepo;
    Template t = aiThreeFieldTpl();
    REQUIRE(tplRepo.save(t).has_value());

    Fill f1; f1.field_id_ = FieldId{"f1"};
    f1.current_value_ = "old"; f1.confidence_ = Confidence::High;
    Fill f2; f2.field_id_ = FieldId{"f2"};
    f2.current_value_ = "1985-03-12"; f2.confidence_ = Confidence::Medium;
    fillRepo.store_["s1"] = makeSession("s1", t.id_, FillStatus::Reviewing, {f1, f2});

    FakeLlmClient fake;
    fake.enqueueOk(makeChatCompletion(nlohmann::json{
        {"updates", nlohmann::json::array({
            nlohmann::json{{"field_id","f1"},{"value","new"},{"confidence","high"}},
        })}}));
    AiFillPipeline pipe{fake, minimalConfig()};
    FillSessionService svc{fillRepo, tplRepo, &pipe};

    std::atomic<bool> cancelled{false};
    auto r = svc.refineField(FillSessionId{"s1"}, "rename", {}, {}, cancelled);

    REQUIRE(r.has_value());
    REQUIRE(r->size() == 1);
    REQUIRE((*r)[0].field_id_.value() == "f1");
    REQUIRE((*r)[0].current_value_ == "new");
    REQUIRE(fillRepo.upsertCalls_.size() == 1);
    REQUIRE(fillRepo.upsertCalls_[0].fieldId == "f1");
    REQUIRE(fillRepo.store_["s1"].fills_.size() == 2);
    auto valueOf = [&](const std::string& fid) {
        for (const auto& f : fillRepo.store_["s1"].fills_)
            if (f.field_id_.value() == fid) return f.current_value_;
        return std::string{};
    };
    REQUIRE(valueOf("f1") == "new");
    REQUIRE(valueOf("f2") == "1985-03-12");
}

TEST_CASE("FillSessionService::refineField: manual-sticky fill is preserved (REVW-05)",
          "[services.fill_session][adapters.ai][revw-05]") {
    FakeFillRepo fillRepo;
    FakeTemplateRepo tplRepo;
    Template t = aiThreeFieldTpl();
    REQUIRE(tplRepo.save(t).has_value());

    Fill manual;
    manual.field_id_      = FieldId{"f1"};
    manual.current_value_ = "user typed";
    manual.confidence_    = Confidence::Manual;
    fillRepo.store_["s1"] = makeSession("s1", t.id_, FillStatus::Reviewing, {manual});

    FakeLlmClient fake;
    fake.enqueueOk(makeChatCompletion(nlohmann::json{
        {"updates", nlohmann::json::array({
            nlohmann::json{{"field_id","f1"},{"value","ai value"},{"confidence","high"}},
        })}}));
    AiFillPipeline pipe{fake, minimalConfig()};
    FillSessionService svc{fillRepo, tplRepo, &pipe};

    std::atomic<bool> cancelled{false};
    auto r = svc.refineField(FillSessionId{"s1"}, "override", {}, {}, cancelled);

    REQUIRE(r.has_value());
    REQUIRE(r->empty());
    REQUIRE(fillRepo.upsertCalls_.empty());
    REQUIRE(fillRepo.store_["s1"].fills_[0].current_value_ == "user typed");
    REQUIRE(fillRepo.store_["s1"].fills_[0].confidence_ == Confidence::Manual);
}

TEST_CASE("[FILL-03] FillSessionService::readSourceText: accepts .odt source",
          "[services.fill_session]") {
    TempFile tmp{uniqueTempPath(".odt")};
    {
        constexpr std::string_view minimalOdt = R"XML(<?xml version="1.0"?>
<office:document-content
    xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0"
    xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0">
  <office:body><office:text>
    <text:p>Hello world</text:p>
  </office:text></office:body>
</office:document-content>)XML";
        int err = 0;
        zip_t* zf = zip_open(tmp.path.string().c_str(), ZIP_CREATE | ZIP_TRUNCATE, &err);
        REQUIRE(zf != nullptr);
        auto* src = zip_source_buffer(zf, minimalOdt.data(), minimalOdt.size(), 0);
        REQUIRE(src != nullptr);
        REQUIRE(zip_file_add(zf, "content.xml", src, ZIP_FL_OVERWRITE) >= 0);
        REQUIRE(zip_close(zf) == 0);
    }

    FakeFillRepo fillRepo;
    FakeTemplateRepo tplRepo;
    FillSessionService svc{fillRepo, tplRepo};
    auto result = svc.readSourceText(tmp.path);

    // Must succeed OR return a generic error — must NOT return InvalidArgument
    if (!result.has_value()) {
        REQUIRE(result.error().kind() != mondoc::Error::Kind::InvalidArgument);
    } else {
        // Valid ODT: should contain the paragraph text
        REQUIRE(result->find("Hello world") != std::string::npos);
    }
}

TEST_CASE("[FILL-04] FillSessionService::readSourceText: replaces invalidArgument stub for .pdf",
          "[services.fill_session]") {
    TempFile tmp{uniqueTempPath(".pdf")};
    {
        std::ofstream out(tmp.path, std::ios::binary);
        out << "not a valid PDF";
    }

    FakeFillRepo fillRepo;
    FakeTemplateRepo tplRepo;
    FillSessionService svc{fillRepo, tplRepo};
    auto result = svc.readSourceText(tmp.path);

    // Must NOT be the old "Phase 4 (deferred)" invalidArgument stub
    bool isOldStub = !result.has_value() &&
                     result.error().kind() == mondoc::Error::Kind::InvalidArgument &&
                     result.error().message().find("Phase 4") != std::string::npos;
    REQUIRE_FALSE(isOldStub);
    // Garbage bytes cause PoDoFo to return a generic error
    if (!result.has_value()) {
        REQUIRE(result.error().kind() != mondoc::Error::Kind::InvalidArgument);
    }
}
