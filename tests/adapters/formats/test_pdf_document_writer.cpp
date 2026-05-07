#include <catch2/catch_test_macros.hpp>

#include <podofo/podofo.h>

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include "pdf_document_writer.hpp"
#include "domain/field.hpp"
#include "domain/fill.hpp"
#include "domain/template.hpp"

using mondoc::FieldId;
using mondoc::TemplateId;
using mondoc::adapters::formats::PdfDocumentWriter;
using mondoc::domain::Field;
using mondoc::domain::FieldType;
using mondoc::domain::Fill;
using mondoc::domain::Template;

namespace {

std::filesystem::path uniqueTempPath(const std::string& ext) {
    static std::mt19937_64 rng{std::random_device{}()};
    auto suffix = std::to_string(rng()) + "_" +
                  std::to_string(std::chrono::steady_clock::now()
                                     .time_since_epoch().count());
    return std::filesystem::temp_directory_path()
        / ("mondoc_test_pdf_" + suffix + ext);
}

struct TempFile {
    std::filesystem::path path;
    explicit TempFile(std::filesystem::path p) : path(std::move(p)) {}
    ~TempFile() { std::error_code ec; std::filesystem::remove(path, ec); }
};

std::vector<unsigned char> readBytes(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::vector<unsigned char>(
        std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{});
}

bool containsAscii(const std::vector<unsigned char>& bytes,
                   std::string_view needle) {
    if (needle.empty() || bytes.size() < needle.size()) return false;
    for (std::size_t i = 0; i + needle.size() <= bytes.size(); ++i) {
        if (std::memcmp(bytes.data() + i, needle.data(), needle.size()) == 0) {
            return true;
        }
    }
    return false;
}

}  // namespace

TEST_CASE("PdfDocumentWriter: writes a valid PDF that PoDoFo can re-parse",
          "[formats.pdf_writer]") {
    TempFile tmp{uniqueTempPath(".pdf")};
    Template tpl;
    tpl.id_ = TemplateId{"t1"};
    tpl.name_ = "Invoice";
    tpl.source_format_ = "docx";
    tpl.fields_.push_back({FieldId{"f1"}, "customer", FieldType::Text});
    tpl.fields_.push_back({FieldId{"f2"}, "amount",   FieldType::Number});
    std::vector<Fill> fills;
    fills.push_back({FieldId{"f1"}, "Acme", {}});
    fills.push_back({FieldId{"f2"}, "42",   {}});

    auto result = PdfDocumentWriter{}.write(tpl, fills, tmp.path);
    REQUIRE(result.has_value());
    REQUIRE(std::filesystem::exists(tmp.path));
    REQUIRE(std::filesystem::file_size(tmp.path) > 100);

    PoDoFo::PdfMemDocument loaded;
    REQUIRE_NOTHROW(loaded.Load(tmp.path.string()));
    REQUIRE(loaded.GetPages().GetCount() >= 1);
}

TEST_CASE("PdfDocumentWriter: output bytes contain template name and each field name + value",
          "[formats.pdf_writer]") {
    TempFile tmp{uniqueTempPath(".pdf")};
    Template tpl;
    tpl.id_ = TemplateId{"t1"};
    tpl.name_ = "Invoice";
    tpl.source_format_ = "docx";
    tpl.fields_.push_back({FieldId{"f1"}, "customer", FieldType::Text});
    tpl.fields_.push_back({FieldId{"f2"}, "amount",   FieldType::Number});
    std::vector<Fill> fills;
    fills.push_back({FieldId{"f1"}, "Acme", {}});
    fills.push_back({FieldId{"f2"}, "42",   {}});

    auto result = PdfDocumentWriter{}.write(tpl, fills, tmp.path);
    REQUIRE(result.has_value());

    auto bytes = readBytes(tmp.path);
    REQUIRE(containsAscii(bytes, "Invoice"));
    REQUIRE(containsAscii(bytes, "customer"));
    REQUIRE(containsAscii(bytes, "Acme"));
    REQUIRE(containsAscii(bytes, "amount"));
    REQUIRE(containsAscii(bytes, "42"));
}

TEST_CASE("PdfDocumentWriter: succeeds when template source_path_ is empty",
          "[formats.pdf_writer]") {
    TempFile tmp{uniqueTempPath(".pdf")};
    Template tpl;
    tpl.id_ = TemplateId{"t1"};
    tpl.name_ = "Empty Source";
    std::vector<Fill> fills{{FieldId{"f1"}, "x", {}}};
    auto result = PdfDocumentWriter{}.write(tpl, fills, tmp.path);
    REQUIRE(result.has_value());
}
