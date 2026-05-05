#include <catch2/catch_test_macros.hpp>

#include "domain/i_document_reader.hpp"
#include "domain/i_template_repository.hpp"

using namespace mondoc;
using namespace mondoc::domain;

namespace {

class FakeReader : public IDocumentReader {
public:
    std::expected<Template, mondoc::Error>
    read(const std::filesystem::path& path) override {
        Template t;
        t.name_ = path.filename().string();
        return t;
    }
};

class FakeRepository : public ITemplateRepository {
public:
    std::expected<void, mondoc::Error> save(const Template&) override {
        return {};
    }
    std::expected<Template, mondoc::Error> findById(const TemplateId& id) override {
        if (id.value() == "missing") {
            return std::unexpected(mondoc::Error::notFound("nope"));
        }
        Template t;
        t.id_ = id;
        return t;
    }
    std::expected<std::vector<Template>, mondoc::Error> listAll() override {
        return std::vector<Template>{};
    }
};

}  // namespace

TEST_CASE("IDocumentReader: vtable resolves through base pointer", "[domain.ports]") {
    FakeReader concrete;
    IDocumentReader* base = &concrete;
    auto result = base->read(std::filesystem::path{"hello.docx"});
    REQUIRE(result.has_value());
    REQUIRE(result->name_ == "hello.docx");
}

TEST_CASE("ITemplateRepository: success and not-found paths compile", "[domain.ports]") {
    FakeRepository concrete;
    ITemplateRepository* base = &concrete;

    REQUIRE(base->save(Template{}).has_value());

    auto found = base->findById(TemplateId{"t-1"});
    REQUIRE(found.has_value());
    REQUIRE(found->id_.value() == "t-1");

    auto missing = base->findById(TemplateId{"missing"});
    REQUIRE_FALSE(missing.has_value());
    REQUIRE(missing.error().kind() == mondoc::Error::Kind::NotFound);

    auto all = base->listAll();
    REQUIRE(all.has_value());
    REQUIRE(all->empty());
}
