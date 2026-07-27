#include <catch2/catch_test_macros.hpp>

#include "mondoc/util.hpp"

#include <filesystem>
#include <string>

TEST_CASE("lowercaseExtension normalizes case", "[domain.util]") {
    CHECK(mondoc::lowercaseExtension("a/b/Doc.PDF") == ".pdf");
    CHECK(mondoc::hasExtension("x.Pdf", ".pdf"));
    CHECK_FALSE(mondoc::hasExtension("x.pdfx", ".pdf"));
}

TEST_CASE("generateUuid returns 36-char unique ids", "[domain.util]") {
    auto a = mondoc::generateUuid();
    auto b = mondoc::generateUuid();
    CHECK(a.size() == 36);
    CHECK(a != b);
}

TEST_CASE("pathToUtf8 round-trips non-ascii", "[domain.util]") {
    std::filesystem::path p = std::filesystem::path(std::u8string{u8"héllo.txt"});
    CHECK(mondoc::pathToUtf8(p) == std::string(reinterpret_cast<const char*>(u8"héllo.txt")));
}
