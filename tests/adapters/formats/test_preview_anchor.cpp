#include <catch2/catch_test_macros.hpp>

#include <podofo/podofo.h>

#include <filesystem>
#include <string>

#include "preview_anchor.hpp"
#include "domain/field.hpp"

#include "support/temp_files.hpp"

using mondoc::adapters::formats::anchorForPreviewRect;

namespace {

std::filesystem::path uniqueTempPath(const std::string& ext) {
    return mondoc::tests_support::uniqueTempPath("mondoc_test_anchor_", ext);
}

using mondoc::tests_support::TempFile;

std::filesystem::path buildFixturePdf() {
    auto path = uniqueTempPath(".pdf");
    PoDoFo::PdfMemDocument doc;
    auto& page = doc.GetPages().CreatePage(
        PoDoFo::PdfPage::CreateStandardPageSize(PoDoFo::PdfPageSize::A4));

    PoDoFo::PdfFontSearchParams params;
    params.AutoSelect = PoDoFo::PdfFontAutoSelectBehavior::Standard14;
    auto* font = doc.GetFonts().SearchFont("Helvetica", params);
    REQUIRE(font != nullptr);

    PoDoFo::PdfPainter painter;
    painter.SetCanvas(page);
    painter.TextState.SetFont(*font, 12);
    painter.DrawText("alpha marker", 100, 700);
    painter.DrawText("unique needle", 100, 500);
    painter.DrawText("alpha marker", 100, 300);
    painter.FinishDrawing();
    doc.Save(path.string());
    return path;
}

}  // namespace

TEST_CASE("anchorForPreviewRect: nearest entry wins and maps into plain text", "[formats.anchor]") {
    TempFile tmp{buildFixturePdf()};
    const std::string plainText = "alpha marker\nunique needle\nalpha marker\n";

    // A4 = 595x842pt, drawn at y=300 from bottom => normalized top-left y ~=
    // (842-300-12)/842. Use a generous rect.
    mondoc::domain::PdfLocation frame{0, 100.0 / 595, (842.0 - 320) / 842, 200.0 / 595, 40.0 / 842};
    auto anchor = anchorForPreviewRect(tmp.path, frame, plainText);
    REQUIRE(anchor.has_value());
    REQUIRE(anchor->excerpt == "alpha marker");
    // second occurrence in the plain text, not the first
    REQUIRE(anchor->char_offset == static_cast<int>(plainText.rfind("alpha marker")));
}

TEST_CASE("anchorForPreviewRect: frame far from any text yields nullopt", "[formats.anchor]") {
    TempFile tmp{buildFixturePdf()};
    const std::string plainText = "alpha marker\nunique needle\nalpha marker\n";

    mondoc::domain::PdfLocation frame{0, 0.85, 0.02, 0.1, 0.03};  // empty corner
    REQUIRE_FALSE(anchorForPreviewRect(tmp.path, frame, plainText).has_value());
}
