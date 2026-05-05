#include <catch2/catch_test_macros.hpp>

#include "mondoc/id.hpp"
#include "mondoc/error.hpp"
#include "domain/template.hpp"
#include "domain/field.hpp"
#include "domain/source_ref.hpp"
#include "domain/fill.hpp"
#include "domain/fill_session.hpp"

#include <type_traits>

using namespace mondoc;
using namespace mondoc::domain;

TEST_CASE("Id types are not interchangeable", "[domain.types]") {
    static_assert(!std::is_assignable_v<TemplateId&, FieldId>,
                  "TemplateId must not be assignable from FieldId");
    static_assert(!std::is_constructible_v<TemplateId, FieldId>,
                  "TemplateId must not be constructible from FieldId");
    SUCCEED();
}

TEST_CASE("Id stores its value and supports equality", "[domain.types]") {
    TemplateId a{"t-1"};
    TemplateId b{"t-1"};
    TemplateId c{"t-2"};
    REQUIRE(a == b);
    REQUIRE(a != c);
    REQUIRE(a.value() == "t-1");
}

TEST_CASE("Field carries id, name, type", "[domain.types]") {
    Field f{FieldId{"f-1"}, "patient_name", FieldType::Text};
    REQUIRE(f.id_.value() == "f-1");
    REQUIRE(f.name_ == "patient_name");
    REQUIRE(f.type_ == FieldType::Text);
}

TEST_CASE("Field defaults to FieldType::Text", "[domain.types]") {
    Field f;
    REQUIRE(f.type_ == FieldType::Text);
    REQUIRE(f.name_.empty());
}

TEST_CASE("Template default-constructs empty", "[domain.types]") {
    Template t;
    REQUIRE(t.fields_.empty());
    REQUIRE(t.name_.empty());
    REQUIRE(t.source_format_.empty());
}

TEST_CASE("Template aggregate-init carries fields", "[domain.types]") {
    Template t{TemplateId{"t-1"}, "Discharge", "docx",
               {Field{FieldId{"f-1"}, "name", FieldType::Text}}};
    REQUIRE(t.id_.value() == "t-1");
    REQUIRE(t.name_ == "Discharge");
    REQUIRE(t.source_format_ == "docx");
    REQUIRE(t.fields_.size() == 1);
    REQUIRE(t.fields_[0].name_ == "name");
}

TEST_CASE("SourceRef carries id, range, excerpt", "[domain.types]") {
    SourceRef ref{SourceDocId{"src-1"}, TextRange{10, 20}, "hello"};
    REQUIRE(ref.source_id_.value() == "src-1");
    REQUIRE(ref.range_.begin_ == 10);
    REQUIRE(ref.range_.end_ == 20);
    REQUIRE(ref.excerpt_ == "hello");
}

TEST_CASE("Fill carries field id, value, source refs", "[domain.types]") {
    Fill fill{FieldId{"f-1"}, "Alice",
              {SourceRef{SourceDocId{"src-1"}, TextRange{0, 5}, "Alice"}}};
    REQUIRE(fill.field_id_.value() == "f-1");
    REQUIRE(fill.current_value_ == "Alice");
    REQUIRE(fill.source_refs_.size() == 1);
}

TEST_CASE("FillSession default status is Created", "[domain.types]") {
    FillSession s;
    REQUIRE(s.status_ == FillStatus::Created);
    REQUIRE(s.fills_.empty());
}

TEST_CASE("Error factory functions tag the kind", "[domain.types]") {
    auto e1 = Error::storageOpen("disk");
    REQUIRE(e1.kind() == Error::Kind::StorageOpen);
    REQUIRE(e1.message() == "disk");

    auto e2 = Error::notFound("missing");
    REQUIRE(e2.kind() == Error::Kind::NotFound);
    REQUIRE(e2.message() == "missing");
}
