#pragma once

#include <regex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace mondoc::adapters::formats::detail {

// The three placeholder syntaxes recognized across text/docx/odt templates:
// {{name}}, [name], <name>.
struct PlaceholderPatterns {
    static const std::regex kDoubleBrace;
    static const std::regex kSquareBracket;
    static const std::regex kAngleBracket;
};

std::string normalize(std::string_view raw);

// Scans `text` for all three placeholder syntaxes and returns the normalized,
// deduplicated field names in encounter order (double-brace, then
// square-bracket, then angle-bracket).
std::vector<std::string> scanPlaceholders(const std::string& text);

// Substitutes all three placeholder syntaxes in a single left-to-right scan
// of `original`, so a fill value that happens to contain placeholder-like
// text (e.g. "John [note] Smith") is never re-scanned. Placeholders with no
// matching entry in `fills` (keyed by detail::normalize'd field name) are
// left verbatim.
std::string substituteAll(const std::string& original,
                          const std::unordered_map<std::string, std::string>& fills);

}  // namespace mondoc::adapters::formats::detail
