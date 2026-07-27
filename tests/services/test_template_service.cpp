#include <catch2/catch_test_macros.hpp>

#include "template_service.hpp"
#include "domain/field.hpp"
#include "domain/i_template_repository.hpp"
#include "domain/template.hpp"
#include "mondoc/error.hpp"
#include "mondoc/id.hpp"

#include "support/temp_files.hpp"

#include <algorithm>
#include <filesystem>
#include <map>
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
        if (it == store_.end()) {
            return mondoc::unexpected(Error::notFound("not in fake repo"));
        }
        return it->second;
    }

    mondoc::expected<std::vector<Template>, Error> listAll() override {
        std::vector<Template> out;
        out.reserve(store_.size());
        for (const auto& [_, t] : store_) {
            out.push_back(t);
        }
        std::sort(out.begin(), out.end(),
                  [](const Template& a, const Template& b) {
                      return a.name_ < b.name_;
                  });
        return out;
    }

    mondoc::expected<void, Error> remove(const TemplateId& id) override {
        auto erased = store_.erase(id.value());
        if (erased == 0) {
            return mondoc::unexpected(Error::notFound("not in fake repo"));
        }
        return {};
    }

    std::size_t size() const noexcept { return store_.size(); }

private:
    std::map<std::string, Template> store_;
};

std::filesystem::path uniqueTempPath(const std::string& ext) {
    return mondoc::tests_support::uniqueTempPath("mondoc_test_service_", ext);
}

using mondoc::tests_support::writeFile;
using mondoc::tests_support::TempFile;

Template makeTemplate(const std::string& id, const std::string& name) {
    Template t;
    t.id_            = TemplateId{id};
    t.name_          = name;
    t.source_format_ = "txt";
    return t;
}

Field makeField(const std::string& id, const std::string& name, FieldType type) {
    Field f;
    f.id_   = FieldId{id};
    f.name_ = name;
    f.type_ = type;
    return f;
}

}  // namespace

TEST_CASE("TemplateService: extractDraft returns error for unsupported extension",
          "[services.template_service]") {
    FakeRepository repo;
    TemplateService svc{repo, std::filesystem::temp_directory_path()};

    auto result = svc.extractDraft(std::filesystem::path{"diagram.png"});

    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().kind() == Error::Kind::InvalidArgument);
}

TEST_CASE("TemplateService: extractDraft reads a .txt file and returns Template draft",
          "[services.template_service]") {
    TempFile tmp{uniqueTempPath(".txt")};
    writeFile(tmp.path, "Hello {{field_one}}!");

    FakeRepository repo;
    TemplateService svc{repo, std::filesystem::temp_directory_path()};

    auto result = svc.extractDraft(tmp.path);

    REQUIRE(result.has_value());
    REQUIRE(result->draft.fields_.size() == 1);
    REQUIRE(result->draft.fields_[0].name_ == "field_one");
}

TEST_CASE("TemplateService: saveTemplate persists to repository",
          "[services.template_service]") {
    FakeRepository repo;
    TemplateService svc{repo, std::filesystem::temp_directory_path()};

    Template t = makeTemplate("t-1", "First");

    auto saved = svc.saveTemplate(t);

    REQUIRE(saved.has_value());
    auto list = svc.listTemplates();
    REQUIRE(list.has_value());
    REQUIRE(list->size() == 1);
    REQUIRE(list->front().id_.value() == "t-1");
}

TEST_CASE("TemplateService: listTemplates returns empty when no templates",
          "[services.template_service]") {
    FakeRepository repo;
    TemplateService svc{repo, std::filesystem::temp_directory_path()};

    auto list = svc.listTemplates();

    REQUIRE(list.has_value());
    REQUIRE(list->empty());
}

TEST_CASE("TemplateService: listTemplates returns saved templates",
          "[services.template_service]") {
    FakeRepository repo;
    TemplateService svc{repo, std::filesystem::temp_directory_path()};

    Template a = makeTemplate("t-a", "Alpha");
    Template b = makeTemplate("t-b", "Beta");
    REQUIRE(svc.saveTemplate(a).has_value());
    REQUIRE(svc.saveTemplate(b).has_value());

    auto list = svc.listTemplates();

    REQUIRE(list.has_value());
    REQUIRE(list->size() == 2);
}

TEST_CASE("TemplateService: deleteTemplate removes the template",
          "[services.template_service]") {
    FakeRepository repo;
    TemplateService svc{repo, std::filesystem::temp_directory_path()};

    Template t = makeTemplate("t-1", "Doomed");
    REQUIRE(svc.saveTemplate(t).has_value());

    auto deleted = svc.deleteTemplate(t.id_);

    REQUIRE(deleted.has_value());
    auto list = svc.listTemplates();
    REQUIRE(list.has_value());
    REQUIRE(list->empty());
}

TEST_CASE("TemplateService: deleteTemplate returns not-found for unknown id",
          "[services.template_service]") {
    FakeRepository repo;
    TemplateService svc{repo, std::filesystem::temp_directory_path()};

    auto result = svc.deleteTemplate(TemplateId{"missing"});

    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().kind() == Error::Kind::NotFound);
}

TEST_CASE("TemplateService: renameTemplate updates the name",
          "[services.template_service]") {
    FakeRepository repo;
    TemplateService svc{repo, std::filesystem::temp_directory_path()};

    Template t = makeTemplate("t-1", "Old");
    REQUIRE(svc.saveTemplate(t).has_value());

    auto renamed = svc.renameTemplate(t.id_, "New");

    REQUIRE(renamed.has_value());
    auto reloaded = repo.findById(t.id_);
    REQUIRE(reloaded.has_value());
    REQUIRE(reloaded->name_ == "New");
}

TEST_CASE("TemplateService: renameTemplate returns not-found for unknown id",
          "[services.template_service]") {
    FakeRepository repo;
    TemplateService svc{repo, std::filesystem::temp_directory_path()};

    auto result = svc.renameTemplate(TemplateId{"missing"}, "Whatever");

    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().kind() == Error::Kind::NotFound);
}

TEST_CASE("TemplateService: renameTemplate rejects a name that collides with an existing template",
          "[services.template_service]") {
    FakeRepository repo;
    TemplateService svc{repo, std::filesystem::temp_directory_path()};

    Template a = makeTemplate("t-a", "Alpha");
    Template b = makeTemplate("t-b", "Beta");
    REQUIRE(svc.saveTemplate(a).has_value());
    REQUIRE(svc.saveTemplate(b).has_value());

    auto result = svc.renameTemplate(b.id_, "Alpha");

    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().kind() == Error::Kind::Conflict);
    REQUIRE(result.error().message() == "Alpha");

    auto reloaded = repo.findById(b.id_);
    REQUIRE(reloaded.has_value());
    REQUIRE(reloaded->name_ == "Beta");
}

TEST_CASE("TemplateService: duplicateTemplate creates new template with new id",
          "[services.template_service]") {
    FakeRepository repo;
    TemplateService svc{repo, std::filesystem::temp_directory_path()};

    Template t = makeTemplate("t-1", "Original");
    t.fields_.push_back(makeField("f-1", "field_one", FieldType::Text));
    t.fields_.push_back(makeField("f-2", "field_two", FieldType::Date));
    REQUIRE(svc.saveTemplate(t).has_value());

    auto dup = svc.duplicateTemplate(t.id_, "Copy");

    REQUIRE(dup.has_value());
    REQUIRE(dup->value() != t.id_.value());

    auto list = svc.listTemplates();
    REQUIRE(list.has_value());
    REQUIRE(list->size() == 2);

    auto copy = repo.findById(*dup);
    REQUIRE(copy.has_value());
    REQUIRE(copy->name_ == "Copy");
    REQUIRE(copy->fields_.size() == 2);
    REQUIRE(copy->fields_[0].id_.value() != "f-1");
    REQUIRE(copy->fields_[1].id_.value() != "f-2");
    REQUIRE(copy->fields_[0].name_ == "field_one");
    REQUIRE(copy->fields_[1].name_ == "field_two");
}

TEST_CASE("TemplateService: duplicateTemplate returns not-found for unknown source id",
          "[services.template_service]") {
    FakeRepository repo;
    TemplateService svc{repo, std::filesystem::temp_directory_path()};

    auto result = svc.duplicateTemplate(TemplateId{"missing"}, "X");

    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().kind() == Error::Kind::NotFound);
}

TEST_CASE("TemplateService: duplicateTemplate rejects a name that collides with an existing template",
          "[services.template_service]") {
    FakeRepository repo;
    TemplateService svc{repo, std::filesystem::temp_directory_path()};

    Template a = makeTemplate("t-a", "Alpha");
    Template b = makeTemplate("t-b", "Beta");
    REQUIRE(svc.saveTemplate(a).has_value());
    REQUIRE(svc.saveTemplate(b).has_value());

    auto result = svc.duplicateTemplate(a.id_, "Beta");

    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().kind() == Error::Kind::Conflict);
    REQUIRE(result.error().message() == "Beta");

    auto list = svc.listTemplates();
    REQUIRE(list.has_value());
    REQUIRE(list->size() == 2);
}
