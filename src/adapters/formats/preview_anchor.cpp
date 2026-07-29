#include "preview_anchor.hpp"

#include "mondoc/util.hpp"

#include <podofo/podofo.h>

#include <algorithm>
#include <limits>
#include <string>
#include <vector>

namespace mondoc::adapters::formats {

std::optional<mondoc::domain::TextLocation>
anchorForPreviewRect(const std::filesystem::path& previewPdf,
                     const mondoc::domain::PdfLocation& frame,
                     const std::string& plainText) {
    try {
        PoDoFo::PdfMemDocument doc;
        doc.Load(mondoc::pathToUtf8(previewPdf));
        if (frame.page_index < 0 ||
            static_cast<unsigned>(frame.page_index) >= doc.GetPages().GetCount())
            return std::nullopt;
        auto& page = doc.GetPages().GetPageAt(static_cast<unsigned>(frame.page_index));
        const auto pageRect = page.GetRect();

        // Frame center in PDF points (PDF y-axis is bottom-up).
        const double cx = pageRect.X + (frame.x + frame.w / 2) * pageRect.Width;
        const double cy = pageRect.Y + (1.0 - frame.y - frame.h / 2) * pageRect.Height;

        std::vector<PoDoFo::PdfTextEntry> entries;
        page.ExtractTextTo(entries);

        // Nearest entry; also count identical earlier texts for occurrence index.
        const PoDoFo::PdfTextEntry* best = nullptr;
        double bestDist = std::numeric_limits<double>::max();
        for (const auto& e : entries) {
            const double dx = e.X - cx, dy = e.Y - cy;
            const double d = dx * dx + dy * dy;
            if (d < bestDist) { bestDist = d; best = &e; }
        }
        // Reject anchors farther than a quarter page diagonal from the frame.
        const double maxDist = 0.25 * (pageRect.Width * pageRect.Width +
                                       pageRect.Height * pageRect.Height);
        if (!best || bestDist > maxDist * 0.25) return std::nullopt;

        std::string snippet = best->Text;
        // trim whitespace both ends
        const auto b = snippet.find_first_not_of(" \t\r\n");
        const auto e2 = snippet.find_last_not_of(" \t\r\n");
        if (b == std::string::npos) return std::nullopt;
        snippet = snippet.substr(b, e2 - b + 1);

        std::size_t occurrence = 0;
        for (const auto& e : entries) {
            if (&e == best) break;
            if (e.Text.find(snippet) != std::string::npos) ++occurrence;
        }
        std::size_t pos = std::string::npos, from = 0;
        for (std::size_t n = 0; ; ++n) {
            pos = plainText.find(snippet, from);
            if (pos == std::string::npos) return std::nullopt;
            if (n == occurrence) break;
            from = pos + 1;
        }

        mondoc::domain::TextLocation tl;
        tl.char_offset = static_cast<int>(pos);
        tl.char_end = static_cast<int>(pos + snippet.size());
        tl.paragraph_index = static_cast<int>(
            std::count(plainText.begin(), plainText.begin() + pos, '\n'));
        tl.excerpt = snippet.substr(0, 120);
        return tl;
    } catch (...) {
        return std::nullopt;
    }
}

}  // namespace mondoc::adapters::formats
