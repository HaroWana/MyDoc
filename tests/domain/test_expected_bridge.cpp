#include <catch2/catch_test_macros.hpp>
#include "mondoc/expected.hpp"

#include <string>

TEST_CASE("expected bridge: success path", "[expected]") {
    mondoc::expected<int, std::string> e{42};
    REQUIRE(e.has_value());
    REQUIRE(*e == 42);
    REQUIRE(e.value() == 42);
}

TEST_CASE("expected bridge: error path", "[expected]") {
    mondoc::expected<int, std::string> e{mondoc::unexpected("oops")};
    REQUIRE_FALSE(e.has_value());
    REQUIRE(e.error() == "oops");
}

TEST_CASE("expected bridge: void specialisation", "[expected]") {
    mondoc::expected<void, std::string> ok;
    REQUIRE(ok.has_value());

    mondoc::expected<void, std::string> ng{mondoc::unexpected("nope")};
    REQUIRE_FALSE(ng.has_value());
    REQUIRE(ng.error() == "nope");
}
