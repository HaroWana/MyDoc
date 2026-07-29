#include <catch2/catch_test_macros.hpp>

#include <podofo/podofo.h>

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "pdf_document_writer.hpp"
#include "domain/field.hpp"
#include "domain/fill.hpp"
#include "domain/template.hpp"

#include "support/temp_files.hpp"

using mondoc::FieldId;
using mondoc::TemplateId;
using mondoc::adapters::formats::PdfDocumentWriter;
using mondoc::domain::Field;
using mondoc::domain::FieldType;
using mondoc::domain::Fill;
using mondoc::domain::Template;

namespace {

std::filesystem::path uniqueTempPath(const std::string& ext) {
    return mondoc::tests_support::uniqueTempPath("mondoc_test_pdf_", ext);
}

using mondoc::tests_support::TempFile;
using mondoc::tests_support::writeFile;
using mondoc::tests_support::readFile;

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

TEST_CASE("PdfDocumentWriter: CJK/emoji title exports with substituted glyphs",
          "[formats.pdf_writer][fmt-19]") {
    TempFile tmp{uniqueTempPath(".pdf")};
    Template tpl;
    tpl.id_ = TemplateId{"t1"};
    tpl.name_ = "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E \xF0\x9F\x98\x80"; // "日本語 😀"
    std::vector<Fill> fills{{FieldId{"f1"}, "x", {}}};

    auto result = PdfDocumentWriter{}.write(tpl, fills, tmp.path);
    REQUIRE(result.has_value());
    REQUIRE(contains(extractAllText(tmp.path), "?"));
}

TEST_CASE("PdfDocumentWriter: unwritable dest path returns an error",
          "[formats.pdf_writer]") {
    Template tpl;
    tpl.id_ = TemplateId{"t1"};
    tpl.name_ = "Doomed";
    std::vector<Fill> fills{{FieldId{"f1"}, "x", {}}};

    const auto dest = uniqueTempPath("") / "no-such-dir" / "out.pdf";
    auto result = PdfDocumentWriter{}.write(tpl, fills, dest);
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("PdfDocumentWriter: long values wrap instead of overflowing",
          "[formats.pdf_writer][fmt-19]") {
    TempFile tmp{uniqueTempPath(".pdf")};
    Template tpl;
    tpl.id_ = TemplateId{"t1"};
    tpl.name_ = "Wrap";
    tpl.source_format_ = "docx";
    tpl.fields_.push_back({FieldId{"f1"}, "notes", FieldType::Paragraph});
    std::string longValue;
    for (int i = 0; i < 40; ++i) longValue += "wrappedword" + std::to_string(i) + " ";
    std::vector<Fill> fills;
    fills.push_back({FieldId{"f1"}, longValue, {}});

    auto result = PdfDocumentWriter{}.write(tpl, fills, tmp.path);
    REQUIRE(result.has_value());

    const std::string text = extractAllText(tmp.path);
    REQUIRE(contains(text, "wrappedword0"));
    REQUIRE(contains(text, "wrappedword39"));

    // A4 body width fits ~90 chars at 12pt; 40 x ~13-char words cannot be one line.
    PoDoFo::PdfMemDocument loaded;
    loaded.Load(tmp.path.string());
    std::vector<PoDoFo::PdfTextEntry> entries;
    loaded.GetPages().GetPageAt(0).ExtractTextTo(entries);
    REQUIRE(entries.size() > 3);
}

TEST_CASE("PdfDocumentWriter: embedded newlines are honored",
          "[formats.pdf_writer][fmt-19]") {
    TempFile tmp{uniqueTempPath(".pdf")};
    Template tpl;
    tpl.id_ = TemplateId{"t1"};
    tpl.name_ = "Newlines";
    tpl.source_format_ = "docx";
    tpl.fields_.push_back({FieldId{"f1"}, "notes", FieldType::Paragraph});
    std::vector<Fill> fills;
    fills.push_back({FieldId{"f1"}, "first line\nsecond line", {}});

    auto result = PdfDocumentWriter{}.write(tpl, fills, tmp.path);
    REQUIRE(result.has_value());
    const std::string text = extractAllText(tmp.path);
    REQUIRE(contains(text, "first line"));
    REQUIRE(contains(text, "second line"));
}

TEST_CASE("PdfDocumentWriter: very long content paginates",
          "[formats.pdf_writer][fmt-19]") {
    TempFile tmp{uniqueTempPath(".pdf")};
    Template tpl;
    tpl.id_ = TemplateId{"t1"};
    tpl.name_ = "Paginate";
    tpl.source_format_ = "docx";
    tpl.fields_.push_back({FieldId{"f1"}, "notes", FieldType::Paragraph});
    std::string huge;
    for (int i = 0; i < 250; ++i) huge += "paragraphline" + std::to_string(i) + "\n";
    std::vector<Fill> fills;
    fills.push_back({FieldId{"f1"}, huge, {}});

    auto result = PdfDocumentWriter{}.write(tpl, fills, tmp.path);
    REQUIRE(result.has_value());

    PoDoFo::PdfMemDocument loaded;
    loaded.Load(tmp.path.string());
    REQUIRE(loaded.GetPages().GetCount() >= 2);
    const std::string text = extractAllText(tmp.path);
    REQUIRE(contains(text, "paragraphline0"));
    REQUIRE(contains(text, "paragraphline249"));
}

TEST_CASE("PdfDocumentWriter: non-WinAnsi characters do not fail the export",
          "[formats.pdf_writer][fmt-19]") {
    TempFile tmp{uniqueTempPath(".pdf")};
    Template tpl;
    tpl.id_ = TemplateId{"t1"};
    tpl.name_ = "Unicode";
    tpl.source_format_ = "docx";
    tpl.fields_.push_back({FieldId{"f1"}, "greeting", FieldType::Text});
    std::vector<Fill> fills;
    fills.push_back({FieldId{"f1"}, "\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82 hello", {}});

    auto result = PdfDocumentWriter{}.write(tpl, fills, tmp.path);
    REQUIRE(result.has_value());
    const std::string text = extractAllText(tmp.path);
    REQUIRE(contains(text, "hello"));
    REQUIRE(contains(text, "?"));
}

TEST_CASE("PdfDocumentWriter: pre-existing dest survives a failed final rename",
          "[formats.pdf_writer][fmt-19]") {
    // dest is an existing DIRECTORY: Save to the temp file succeeds, the
    // rename into place fails, and nothing pre-existing may be destroyed.
    const auto dest = uniqueTempPath("");
    std::filesystem::create_directories(dest);

    Template tpl;
    tpl.id_ = TemplateId{"t1"};
    tpl.name_ = "Doomed";
    std::vector<Fill> fills{{FieldId{"f1"}, "x", {}}};

    auto result = PdfDocumentWriter{}.write(tpl, fills, dest);
    REQUIRE_FALSE(result.has_value());
    REQUIRE(std::filesystem::is_directory(dest));

    auto tmpLeftover = dest;
    tmpLeftover += ".mondoc-tmp";
    REQUIRE_FALSE(std::filesystem::exists(tmpLeftover));

    std::error_code ec;
    std::filesystem::remove_all(dest, ec);
}

TEST_CASE("PdfDocumentWriter: non-UTF-8 bytes in the template name are substituted",
          "[formats.pdf_writer][fmt-19]") {
    TempFile tmp{uniqueTempPath(".pdf")};
    Template tpl;
    tpl.id_ = TemplateId{"t1"};
    tpl.name_ = "Factur\xE9 latin1";  // raw Latin-1 0xE9: invalid UTF-8
    std::vector<Fill> fills{{FieldId{"f1"}, "x", {}}};

    auto result = PdfDocumentWriter{}.write(tpl, fills, tmp.path);
    REQUIRE(result.has_value());
    const std::string text = extractAllText(tmp.path);
    REQUIRE(contains(text, "Factur?"));
    REQUIRE(contains(text, "latin1"));
}

TEST_CASE("PdfDocumentWriter: unbroken multi-byte token hard-splits on code-point boundaries",
          "[formats.pdf_writer][fmt-19]") {
    TempFile tmp{uniqueTempPath(".pdf")};
    Template tpl;
    tpl.id_ = TemplateId{"t1"};
    tpl.name_ = "Accents";
    tpl.fields_.push_back({FieldId{"f1"}, "ref", FieldType::Text});
    std::string token;
    for (int i = 0; i < 300; ++i) token += "\xC3\xA9";  // 300 x 'é', one word
    std::vector<Fill> fills{{FieldId{"f1"}, token, {}}};

    auto result = PdfDocumentWriter{}.write(tpl, fills, tmp.path);
    REQUIRE(result.has_value());
    const std::string text = extractAllText(tmp.path);
    REQUIRE(contains(text, "\xC3\xA9\xC3\xA9\xC3\xA9"));
}
