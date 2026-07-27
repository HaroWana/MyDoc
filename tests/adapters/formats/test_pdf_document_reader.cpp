#include <catch2/catch_test_macros.hpp>

#include "pdf_document_reader.hpp"
#include "mondoc/error.hpp"

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

std::filesystem::path uniqueTempPath(const std::string& suffix) {
    static std::mt19937_64 rng{std::random_device{}()};
    auto id = std::to_string(rng());
    return std::filesystem::temp_directory_path() / ("mondoc_test_" + id + suffix);
}

}  // namespace

TEST_CASE("[TMPL-03] PdfDocumentReader: rejects file with wrong extension",
          "[formats.pdf_reader]") {
    PdfDocumentReader reader;
    auto result = reader.read(std::filesystem::path{"foo.docx"});
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().kind() == mondoc::Error::Kind::InvalidArgument);
}

TEST_CASE("[TMPL-03] PdfDocumentReader: returns error for corrupt file",
          "[formats.pdf_reader]") {
    TempFile tmp{uniqueTempPath(".pdf")};
    {
        std::ofstream out(tmp.path, std::ios::binary);
        out << "not a PDF file at all\x00\x01\x02";
    }
    PdfDocumentReader reader;
    auto result = reader.read(tmp.path);
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("PdfDocumentReader: rejects file larger than 50MB",
          "[formats.pdf_reader]") {
    TempFile tmp{uniqueTempPath(".pdf")};
    {
        std::ofstream out(tmp.path, std::ios::binary);
        out << "%PDF-1.4\n";
    }
    std::error_code resizeEc;
    std::filesystem::resize_file(tmp.path, 51ULL * 1024 * 1024, resizeEc);
    REQUIRE_FALSE(resizeEc);

    PdfDocumentReader reader;
    auto result = reader.read(tmp.path);
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().message().find("too large") != std::string::npos);
}

TEST_CASE("[TMPL-06] PdfDocumentReader: extracts AcroForm TextBox field",
          "[formats.pdf_reader]") {
    SKIP("requires real .pdf fixture with AcroForm fields");
}

TEST_CASE("[TMPL-06] PdfDocumentReader: rejects XFA-only PDF with actionable error",
          "[formats.pdf_reader]") {
    SKIP("requires real XFA-only .pdf fixture");
}
