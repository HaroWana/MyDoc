#include <catch2/catch_test_macros.hpp>

#include "pdf_document_writer.hpp"

using mondoc::adapters::formats::PdfDocumentWriter;

TEST_CASE("PdfDocumentWriter: write returns error placeholder (Plan 03)",
          "[formats.pdf_writer][!shouldfail]") {
    PdfDocumentWriter w;
    REQUIRE(false);
}
