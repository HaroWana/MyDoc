#include <catch2/catch_test_macros.hpp>

#include "ai_fill_pipeline.hpp"
#include "domain/confidence.hpp"

using mondoc::adapters::ai::parseConfidence;
using mondoc::domain::Confidence;

TEST_CASE("parseConfidence: high", "[adapters.ai][confidence][fill-10]") {
    REQUIRE(parseConfidence("high") == Confidence::High);
}

TEST_CASE("parseConfidence: medium", "[adapters.ai][confidence][fill-10]") {
    REQUIRE(parseConfidence("medium") == Confidence::Medium);
}

TEST_CASE("parseConfidence: low", "[adapters.ai][confidence][fill-10]") {
    REQUIRE(parseConfidence("low") == Confidence::Low);
}

TEST_CASE("parseConfidence: empty string defaults to Low",
          "[adapters.ai][confidence][fill-10]") {
    REQUIRE(parseConfidence("") == Confidence::Low);
}

TEST_CASE("parseConfidence: case-sensitive (HIGH falls back to Low)",
          "[adapters.ai][confidence][fill-10]") {
    REQUIRE(parseConfidence("HIGH") == Confidence::Low);
}

TEST_CASE("parseConfidence: never returns Manual even on 'manual' input",
          "[adapters.ai][confidence][fill-10]") {
    REQUIRE(parseConfidence("manual") == Confidence::Low);
}
