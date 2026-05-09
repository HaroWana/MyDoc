#include "pdf_document_writer.hpp"

#include <podofo/podofo.h>

#include <algorithm>
#include <exception>
#include <filesystem>
#include <string>

namespace mondoc::adapters::formats {

namespace {

std::string pathToUtf8(const std::filesystem::path& p) {
    auto u8 = p.u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

const mondoc::domain::Field*
findField(const mondoc::domain::Template& tpl, const mondoc::FieldId& id) {
    auto it = std::find_if(tpl.fields_.begin(), tpl.fields_.end(),
        [&](const mondoc::domain::Field& f) { return f.id_ == id; });
    return it == tpl.fields_.end() ? nullptr : &*it;
}

}  // namespace

mondoc::expected<void, mondoc::Error>
PdfDocumentWriter::write(const mondoc::domain::Template& tpl,
                         const std::vector<mondoc::domain::Fill>& fills,
                         const std::filesystem::path& dest) {
    try {
        PoDoFo::PdfMemDocument document;
        auto& page = document.GetPages().CreatePage(
            PoDoFo::PdfPage::CreateStandardPageSize(PoDoFo::PdfPageSize::A4));

        PoDoFo::PdfFontSearchParams params;
        params.AutoSelect = PoDoFo::PdfFontAutoSelectBehavior::Standard14;
        auto* font = document.GetFonts().SearchFont("Helvetica", params);
        if (!font) {
            return mondoc::unexpected(mondoc::Error::generic(
                "podofo: Helvetica font not found"));
        }

        PoDoFo::PdfPainter painter;
        painter.SetCanvas(page);

        const double leftMargin   = 56.69;
        const double topMargin    = 56.69;
        const double bottomMargin = 56.69;
        const double titleSize    = 16.0;
        const double bodySize     = 12.0;
        const double lineHeight   = 16.0;

        double cursorY = page.GetRect().Height - topMargin;

        painter.TextState.SetFont(*font, titleSize);
        painter.DrawText(tpl.name_, leftMargin, cursorY);
        cursorY -= lineHeight * 1.5;

        painter.TextState.SetFont(*font, bodySize);
        for (const auto& fill : fills) {
            const auto* fld = findField(tpl, fill.field_id_);
            const std::string label = fld ? fld->name_ : fill.field_id_.value();
            const std::string line = label + ": " + fill.current_value_;
            if (cursorY < bottomMargin) {
                auto& nextPage = document.GetPages().CreatePage(
                    PoDoFo::PdfPage::CreateStandardPageSize(PoDoFo::PdfPageSize::A4));
                painter.FinishDrawing();
                painter.SetCanvas(nextPage);
                painter.TextState.SetFont(*font, bodySize);
                cursorY = nextPage.GetRect().Height - topMargin;
            }
            painter.DrawText(line, leftMargin, cursorY);
            cursorY -= lineHeight;
        }

        painter.FinishDrawing();
        document.Save(pathToUtf8(dest));
        return {};
    } catch (const PoDoFo::PdfError& e) {
        return mondoc::unexpected(mondoc::Error::generic(
            std::string{"podofo: "} + e.what()));
    } catch (const std::exception& e) {
        return mondoc::unexpected(mondoc::Error::generic(
            std::string{"podofo: "} + e.what()));
    }
}

}  // namespace mondoc::adapters::formats
