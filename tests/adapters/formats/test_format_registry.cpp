#include <catch2/catch_test_macros.hpp>

#include "format_registry.hpp"

#include <filesystem>

using namespace mondoc::adapters::formats;

TEST_CASE("readerForPath: supported source extensions get a reader", "[formats]") {
    for (const char* p : {"a.docx", "a.odt", "a.pdf", "a.txt", "a.md", "A.DOCX"}) {
        REQUIRE(readerForPath(std::filesystem::path{p}) != nullptr);
    }
    REQUIRE(readerForPath(std::filesystem::path{"a.xyz"}) == nullptr);
    REQUIRE(readerForPath(std::filesystem::path{"noext"}) == nullptr);
}

TEST_CASE("writerForExtension: export formats get a writer", "[formats]") {
    for (const char* e : {".docx", ".odt", ".pdf", ".txt"}) {
        REQUIRE(writerForExtension(e) != nullptr);
    }
    REQUIRE(writerForExtension(".md") == nullptr);
    REQUIRE(writerForExtension("docx") == nullptr);
}
