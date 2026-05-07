#include <catch2/catch_test_macros.hpp>

#include "text_document_writer.hpp"

using mondoc::adapters::formats::TextDocumentWriter;

TEST_CASE("TextDocumentWriter: write returns error placeholder (Plan 04)",
          "[formats.text_writer][!shouldfail]") {
    TextDocumentWriter w;
    REQUIRE(false);
}
