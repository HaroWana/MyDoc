#include <catch2/catch_test_macros.hpp>

#include "docx_document_writer.hpp"

using mondoc::adapters::formats::DocxDocumentWriter;

TEST_CASE("DocxDocumentWriter: write returns error placeholder (Plan 02)",
          "[formats.docx_writer][!shouldfail]") {
    DocxDocumentWriter w;
    REQUIRE(false);
}
