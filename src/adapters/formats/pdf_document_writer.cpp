#include "pdf_document_writer.hpp"

#include "mondoc/util.hpp"

#include <podofo/podofo.h>

#include <algorithm>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

namespace mondoc::adapters::formats {

namespace {

const mondoc::domain::Field*
findField(const mondoc::domain::Template& tpl, const mondoc::FieldId& id) {
    auto it = std::find_if(tpl.fields_.begin(), tpl.fields_.end(),
        [&](const mondoc::domain::Field& f) { return f.id_ == id; });
    return it == tpl.fields_.end() ? nullptr : &*it;
}

// Replaces every code point the font's encoding cannot represent with '?',
// so exports never fail on non-WinAnsi input (FMT-19).
std::string substituteUnencodable(const PoDoFo::PdfFont& font, const std::string& text) {
    const auto& enc = font.GetEncoding();
    PoDoFo::charbuff buf;
    if (enc.TryConvertToEncoded(text, buf)) return text;
    std::string out;
    std::size_t i = 0;
    while (i < text.size()) {
        const auto c = static_cast<unsigned char>(text[i]);
        std::size_t len = 1;
        if ((c & 0xE0) == 0xC0) len = 2;
        else if ((c & 0xF0) == 0xE0) len = 3;
        else if ((c & 0xF8) == 0xF0) len = 4;
        len = std::min(len, text.size() - i);
        const std::string cp = text.substr(i, len);
        buf.clear();
        out += enc.TryConvertToEncoded(cp, buf) ? cp : std::string{"?"};
        i += len;
    }
    return out;
}

std::vector<std::string> wrapText(const PoDoFo::PdfFont& font,
                                  const PoDoFo::PdfTextState& state,
                                  const std::string& text,
                                  double maxWidth) {
    std::vector<std::string> lines;
    std::istringstream paragraphs(text);
    std::string paragraph;
    while (std::getline(paragraphs, paragraph)) {
        std::string current;
        std::istringstream words(paragraph);
        std::string word;
        while (words >> word) {
            while (font.GetStringLength(word, state) > maxWidth && word.size() > 1) {
                std::size_t cut = word.size() - 1;
                while (cut > 1 && font.GetStringLength(word.substr(0, cut), state) > maxWidth)
                    --cut;
                while (cut < word.size() &&
                       (static_cast<unsigned char>(word[cut]) & 0xC0) == 0x80)
                    ++cut;  // never split inside a UTF-8 sequence
                if (!current.empty()) {
                    lines.push_back(current);
                    current.clear();
                }
                lines.push_back(word.substr(0, cut));
                word.erase(0, cut);
            }
            const std::string candidate = current.empty() ? word : current + " " + word;
            if (!current.empty() && font.GetStringLength(candidate, state) > maxWidth) {
                lines.push_back(current);
                current = word;
            } else {
                current = candidate;
            }
        }
        lines.push_back(current);
    }
    if (lines.empty()) lines.push_back("");
    return lines;
}

}  // namespace

mondoc::expected<void, mondoc::Error>
PdfDocumentWriter::write(const mondoc::domain::Template& tpl,
                         const std::vector<mondoc::domain::Fill>& fills,
                         const std::filesystem::path& dest) {
    bool destWritten = false;
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
        const double rightMargin  = 56.69;
        const double topMargin    = 56.69;
        const double bottomMargin = 56.69;
        const double titleSize    = 16.0;
        const double bodySize     = 12.0;
        const double lineHeight   = 16.0;
        const double maxWidth = page.GetRect().Width - leftMargin - rightMargin;

        double cursorY = page.GetRect().Height - topMargin;

        auto newPage = [&]() {
            auto& nextPage = document.GetPages().CreatePage(
                PoDoFo::PdfPage::CreateStandardPageSize(PoDoFo::PdfPageSize::A4));
            painter.FinishDrawing();
            painter.SetCanvas(nextPage);
            painter.TextState.SetFont(*font, bodySize);
            cursorY = nextPage.GetRect().Height - topMargin;
        };

        auto drawLine = [&](const std::string& line, double advance) {
            if (cursorY < bottomMargin) newPage();
            if (!line.empty()) painter.DrawText(line, leftMargin, cursorY);
            cursorY -= advance;
        };

        PoDoFo::PdfTextState titleState;
        titleState.FontSize = titleSize;
        painter.TextState.SetFont(*font, titleSize);
        for (const auto& line :
             wrapText(*font, titleState, substituteUnencodable(*font, tpl.name_), maxWidth)) {
            drawLine(line, lineHeight * 1.5);
        }

        PoDoFo::PdfTextState bodyState;
        bodyState.FontSize = bodySize;
        painter.TextState.SetFont(*font, bodySize);
        for (const auto& fill : fills) {
            const auto* fld = findField(tpl, fill.field_id_);
            const std::string label = fld ? fld->name_ : fill.field_id_.value();
            const std::string text =
                substituteUnencodable(*font, label + ": " + fill.current_value_);
            for (const auto& line : wrapText(*font, bodyState, text, maxWidth)) {
                drawLine(line, lineHeight);
            }
        }

        painter.FinishDrawing();
        destWritten = true;
        document.Save(pathToUtf8(dest));
        return {};
    } catch (const PoDoFo::PdfError& e) {
        if (destWritten) {
            std::error_code ec;
            std::filesystem::remove(dest, ec);
        }
        return mondoc::unexpected(mondoc::Error::generic(
            std::string{"podofo: "} + e.what()));
    } catch (const std::exception& e) {
        if (destWritten) {
            std::error_code ec;
            std::filesystem::remove(dest, ec);
        }
        return mondoc::unexpected(mondoc::Error::generic(
            std::string{"podofo: "} + e.what()));
    }
}

}  // namespace mondoc::adapters::formats
