#include <catch2/catch_test_macros.hpp>

#include "template_service.hpp"
#include "domain/field.hpp"
#include "domain/i_template_repository.hpp"
#include "domain/template.hpp"
#include "mondoc/error.hpp"
#include "mondoc/id.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <random>
#include <string>
#include <vector>

using mondoc::Error;
using mondoc::FieldId;
using mondoc::TemplateId;
using mondoc::domain::Field;
using mondoc::domain::FieldType;
using mondoc::domain::ITemplateRepository;
using mondoc::domain::Template;
using mondoc::services::TemplateService;

namespace {

class FakeRepository : public ITemplateRepository {
public:
    mondoc::expected<void, Error> save(const Template& t) override {
        store_[t.id_.value()] = t;
        return {};
    }

    mondoc::expected<Template, Error> findById(const TemplateId& id) override {
        auto it = store_.find(id.value());
        if (it == store_.end())
            return mondoc::unexpected(Error::notFound("not in fake repo"));
        return it->second;
    }

    mondoc::expected<std::vector<Template>, Error> listAll() override {
        std::vector<Template> out;
        out.reserve(store_.size());
        for (const auto& [_, t] : store_)
            out.push_back(t);
        std::sort(out.begin(), out.end(),
                  [](const Template& a, const Template& b) { return a.name_ < b.name_; });
        return out;
    }

    mondoc::expected<void, Error> remove(const TemplateId& id) override {
        if (store_.erase(id.value()) == 0)
            return mondoc::unexpected(Error::notFound("not in fake repo"));
        return {};
    }

    std::size_t size() const noexcept { return store_.size(); }

private:
    std::map<std::string, Template> store_;
};

std::filesystem::path uniqueTempPath(const std::string& ext) {
    static std::mt19937_64 rng{std::random_device{}()};
    auto suffix = std::to_string(rng()) + "_" +
                  std::to_string(std::chrono::steady_clock::now()
                                     .time_since_epoch().count());
    return std::filesystem::temp_directory_path()
           / ("mondoc_test_ei_" + suffix + ext);
}

struct TempFile {
    std::filesystem::path path;
    explicit TempFile(std::filesystem::path p) : path(std::move(p)) {}
    ~TempFile() { std::error_code ec; std::filesystem::remove(path, ec); }
};

Template makeTemplate(const std::string& id, const std::string& name,
                      const std::filesystem::path& src) {
    Template t;
    t.id_            = TemplateId{id};
    t.name_          = name;
    t.source_format_ = "txt";
    t.source_path_   = src;
    Field f;
    f.id_   = FieldId{"f-" + id};
    f.name_ = "placeholder_one";
    f.type_ = FieldType::Text;
    t.fields_.push_back(f);
    return t;
}

}  // namespace

TEST_CASE("TemplateService::exportTemplate: produces a non-empty .mondoc file [phase05][services.template_export_import]") {
    TempFile src{uniqueTempPath(".txt")};
    { std::ofstream f(src.path); f << "Hello {{placeholder_one}}"; }

    FakeRepository repo;
    TemplateService svc{repo};
    auto t = makeTemplate("t-export", "Export Test", src.path);
    REQUIRE(repo.save(t).has_value());

    TempFile bundle{uniqueTempPath(".mondoc")};
    auto result = svc.exportTemplate(t.id_, bundle.path);

    REQUIRE(result.has_value());
    REQUIRE(std::filesystem::exists(bundle.path));
    REQUIRE(std::filesystem::file_size(bundle.path) > 0);
}

TEST_CASE("TemplateService::importTemplate: round-trip preserves name and field count [phase05][services.template_export_import]") {
    TempFile src{uniqueTempPath(".txt")};
    { std::ofstream f(src.path); f << "Hello {{placeholder_one}}"; }

    FakeRepository repo;
    TemplateService svc{repo};
    auto t = makeTemplate("t-rt", "Round-Trip Test", src.path);
    REQUIRE(repo.save(t).has_value());

    TempFile bundle{uniqueTempPath(".mondoc")};
    REQUIRE(svc.exportTemplate(t.id_, bundle.path).has_value());

    REQUIRE(repo.remove(t.id_).has_value());

    auto imported = svc.importTemplate(bundle.path);
    REQUIRE(imported.has_value());
    REQUIRE(imported->name_ == "Round-Trip Test");
    REQUIRE(imported->fields_.size() == 1);
    REQUIRE(imported->fields_[0].name_ == "placeholder_one");
}

TEST_CASE("TemplateService::importTemplate: name collision returns ImportConflict error [phase05][services.template_export_import]") {
    TempFile src{uniqueTempPath(".txt")};
    { std::ofstream f(src.path); f << "Hello {{placeholder_one}}"; }

    FakeRepository repo;
    TemplateService svc{repo};
    auto t = makeTemplate("t-conflict", "Conflict Test", src.path);
    REQUIRE(repo.save(t).has_value());

    TempFile bundle{uniqueTempPath(".mondoc")};
    REQUIRE(svc.exportTemplate(t.id_, bundle.path).has_value());

    // Original still in repo → collision
    auto result = svc.importTemplate(bundle.path);
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().message().find("ImportConflict:") != std::string::npos);
}

TEST_CASE("TemplateService::importTemplate: overwrite=true replaces existing template [phase05][services.template_export_import]") {
    TempFile src{uniqueTempPath(".txt")};
    { std::ofstream f(src.path); f << "Hello {{placeholder_one}}"; }

    FakeRepository repo;
    TemplateService svc{repo};
    auto t = makeTemplate("t-overwrite", "Overwrite Test", src.path);
    REQUIRE(repo.save(t).has_value());

    TempFile bundle{uniqueTempPath(".mondoc")};
    REQUIRE(svc.exportTemplate(t.id_, bundle.path).has_value());

    auto result = svc.importTemplate(bundle.path, /*overwrite=*/true);
    REQUIRE(result.has_value());

    auto list = svc.listTemplates();
    REQUIRE(list.has_value());
    int count = 0;
    for (const auto& tmpl : *list)
        if (tmpl.name_ == "Overwrite Test") ++count;
    REQUIRE(count == 1);
}

TEST_CASE("TemplateService::importTemplate: importAsCopy=true creates renamed copy [phase05][services.template_export_import]") {
    TempFile src{uniqueTempPath(".txt")};
    { std::ofstream f(src.path); f << "Hello {{placeholder_one}}"; }

    FakeRepository repo;
    TemplateService svc{repo};
    auto t = makeTemplate("t-copy", "Copy Test", src.path);
    REQUIRE(repo.save(t).has_value());

    TempFile bundle{uniqueTempPath(".mondoc")};
    REQUIRE(svc.exportTemplate(t.id_, bundle.path).has_value());

    auto result = svc.importTemplate(bundle.path, /*overwrite=*/false, /*importAsCopy=*/true);
    REQUIRE(result.has_value());
    REQUIRE(result->name_ == "Copy Test (imported)");

    auto list = svc.listTemplates();
    REQUIRE(list.has_value());
    REQUIRE(list->size() == 2);
}

TEST_CASE("TemplateService::importTemplate: importAsCopy=true preserves the original template [phase05][services.template_export_import]") {
    TempFile src{uniqueTempPath(".txt")};
    { std::ofstream f(src.path); f << "Hello {{placeholder_one}}"; }

    FakeRepository repo;
    TemplateService svc{repo};
    auto t = makeTemplate("t-copy-preserve", "Preserve Test", src.path);
    REQUIRE(repo.save(t).has_value());

    TempFile bundle{uniqueTempPath(".mondoc")};
    REQUIRE(svc.exportTemplate(t.id_, bundle.path).has_value());

    auto result = svc.importTemplate(bundle.path, /*overwrite=*/false, /*importAsCopy=*/true);
    REQUIRE(result.has_value());
    REQUIRE(result->name_ == "Preserve Test (imported)");
    REQUIRE(result->id_.value() != t.id_.value());
    for (const auto& f : result->fields_) {
        for (const auto& origField : t.fields_) {
            REQUIRE(f.id_.value() != origField.id_.value());
        }
    }

    auto list = svc.listTemplates();
    REQUIRE(list.has_value());
    REQUIRE(list->size() == 2);

    bool foundOriginal = false;
    bool foundCopy = false;
    for (const auto& tmpl : *list) {
        if (tmpl.id_.value() == t.id_.value() && tmpl.name_ == "Preserve Test") {
            foundOriginal = true;
        }
        if (tmpl.name_ == "Preserve Test (imported)" && tmpl.id_.value() != t.id_.value()) {
            foundCopy = true;
        }
    }
    REQUIRE(foundOriginal);
    REQUIRE(foundCopy);
}
