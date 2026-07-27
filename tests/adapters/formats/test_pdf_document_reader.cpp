#include <catch2/catch_test_macros.hpp>

#include "pdf_document_reader.hpp"
#include "domain/field.hpp"
#include "domain/template.hpp"
#include "mondoc/error.hpp"

#include <podofo/podofo.h>

#include <filesystem>
#include <string>

#include "support/temp_files.hpp"

using namespace mondoc::adapters::formats;

namespace {

using mondoc::tests_support::TempFile;
using mondoc::tests_support::writeFile;

std::filesystem::path uniqueTempPath(const std::string& suffix) {
    return mondoc::tests_support::uniqueTempPath("mondoc_test_", suffix);
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
    writeFile(tmp.path, "not a PDF file at all\x00\x01\x02");
    PdfDocumentReader reader;
    auto result = reader.read(tmp.path);
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("PdfDocumentReader: rejects file larger than 50MB",
          "[formats.pdf_reader]") {
    TempFile tmp{uniqueTempPath(".pdf")};
    writeFile(tmp.path, "%PDF-1.4\n");
    std::error_code resizeEc;
    std::filesystem::resize_file(tmp.path, 51ULL * 1024 * 1024, resizeEc);
    REQUIRE_FALSE(resizeEc);

    PdfDocumentReader reader;
    auto result = reader.read(tmp.path);
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().message().find("too large") != std::string::npos);
}

TEST_CASE("[TMPL-06][TST-3] PdfDocumentReader: extracts AcroForm TextBox field",
          "[formats.pdf_reader][tst-3]") {
    TempFile tmp{uniqueTempPath(".pdf")};
    {
        PoDoFo::PdfMemDocument doc;
        auto& page = doc.GetPages().CreatePage(
            PoDoFo::PdfPage::CreateStandardPageSize(PoDoFo::PdfPageSize::A4));
        PoDoFo::Rect rect(50, 700, 200, 20);
        page.CreateField<PoDoFo::PdfTextBox>("customer_name", rect);
        doc.Save(tmp.path.string());
    }

    PdfDocumentReader reader;
    auto result = reader.read(tmp.path);

    REQUIRE(result.has_value());
    REQUIRE(result->fields_.size() == 1);
    REQUIRE(result->fields_[0].name_ == "customer_name");
    REQUIRE(result->fields_[0].type_ == mondoc::domain::FieldType::Text);
    REQUIRE(result->fields_[0].origin_ == mondoc::domain::FieldOrigin::FormControl);
}

TEST_CASE("[TMPL-06][TST-3] PdfDocumentReader: rejects XFA-only PDF with actionable error",
          "[formats.pdf_reader][tst-3]") {
    TempFile tmp{uniqueTempPath(".pdf")};
    {
        PoDoFo::PdfMemDocument doc;
        doc.GetPages().CreatePage(
            PoDoFo::PdfPage::CreateStandardPageSize(PoDoFo::PdfPageSize::A4));
        auto& acroForm = doc.GetOrCreateAcroForm();
        // No regular AcroForm fields — only the XFA key, mirroring a
        // hybrid/XFA-only form exported by tools like Adobe LiveCycle.
        acroForm.GetDictionary().AddKey("XFA", PoDoFo::PdfString("dummy-xfa-stream"));
        doc.Save(tmp.path.string());
    }

    PdfDocumentReader reader;
    auto result = reader.read(tmp.path);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().message().find("XFA") != std::string::npos);
}
