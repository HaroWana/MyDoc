#include <catch2/catch_test_macros.hpp>

#include <podofo/podofo.h>

#include <chrono>
#include <filesystem>
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

std::string extractAllText(const std::filesystem::path& pdfPath) {
    PoDoFo::PdfMemDocument doc;
    doc.Load(pdfPath.string());
    std::string out;
    const unsigned count = doc.GetPages().GetCount();
    for (unsigned i = 0; i < count; ++i) {
        std::vector<PoDoFo::PdfTextEntry> entries;
        doc.GetPages().GetPageAt(i).ExtractTextTo(entries);
        for (const auto& e : entries) {
            out += e.Text;
            out += '\n';
        }
    }
    return out;
}

bool contains(std::string_view haystack, std::string_view needle) {
    return haystack.find(needle) != std::string_view::npos;
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

    const std::string text = extractAllText(tmp.path);
    REQUIRE(contains(text, "Invoice"));
    REQUIRE(contains(text, "customer"));
    REQUIRE(contains(text, "Acme"));
    REQUIRE(contains(text, "amount"));
    REQUIRE(contains(text, "42"));
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
