#include <catch2/catch_test_macros.hpp>

#include "pdf_document_reader.hpp"

#include <filesystem>
#include <fstream>
#include <random>
#include <string>

using namespace mondoc::adapters::formats;

namespace {

struct TempFile {
    std::filesystem::path path;
    explicit TempFile(std::filesystem::path p) : path(std::move(p)) {}
    ~TempFile() { std::error_code ec; std::filesystem::remove(path, ec); }
};

std::filesystem::path uniqueTempPdfPath() {
    static std::mt19937_64 rng{std::random_device{}()};
    auto suffix = std::to_string(rng());
    return std::filesystem::temp_directory_path()
           / ("mondoc_test_" + suffix + ".pdf");
}

}  // namespace

TEST_CASE("[TMPL-03] PdfDocumentReader: rejects file with wrong extension",
          "[formats.pdf_reader]") {
    PdfDocumentReader reader;
    auto result = reader.read(std::filesystem::path{"foo.docx"});
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("[TMPL-03] PdfDocumentReader: returns error for corrupt file",
          "[formats.pdf_reader]") {
    TempFile f{uniqueTempPdfPath()};
    {
        std::ofstream out(f.path, std::ios::binary);
        out << "not a pdf file at all";
    }
    PdfDocumentReader reader;
    auto result = reader.read(f.path);
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("[TMPL-06] PdfDocumentReader: extracts AcroForm TextBox field",
          "[formats.pdf_reader]") {
    FAIL("not yet implemented");
}

TEST_CASE("[TMPL-06] PdfDocumentReader: rejects XFA-only PDF with actionable error",
          "[formats.pdf_reader]") {
    FAIL("not yet implemented");
}
