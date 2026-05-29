#include <catch2/catch_test_macros.hpp>
#include "domain/field.hpp"

using namespace mondoc::domain;

TEST_CASE("FieldLocation: PdfLocation normalized rect is valid [phase05][domain.field_location]") {
    PdfLocation loc;
    loc.page_index = 0;
    loc.x = 0.1; loc.y = 0.2; loc.w = 0.3; loc.h = 0.4;
    REQUIRE(loc.page_index == 0);
    REQUIRE(loc.x == 0.1);
    REQUIRE(loc.w == 0.3);
}

TEST_CASE("FieldLocation: TextLocation paragraph_index and char_offset [phase05][domain.field_location]") {
    TextLocation loc;
    loc.paragraph_index = 2;
    loc.char_offset = 15;
    REQUIRE(loc.paragraph_index == 2);
    REQUIRE(loc.char_offset == 15);
}

TEST_CASE("FieldLocation: FieldLocation optional pdf variant [phase05][domain.field_location]") {
    FieldLocation fl;
    fl.pdf = PdfLocation{0, 0.1, 0.2, 0.3, 0.4};
    REQUIRE(fl.pdf.has_value());
    REQUIRE_FALSE(fl.text.has_value());
    REQUIRE(fl.pdf->page_index == 0);
}

TEST_CASE("FieldLocation: Field::location_ defaults to nullopt [phase05][domain.field_location]") {
    Field f;
    REQUIRE_FALSE(f.location_.has_value());
}
