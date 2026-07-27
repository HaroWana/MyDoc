#pragma once

#include <regex>
#include <string>
#include <string_view>
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

}  // namespace mondoc::adapters::formats::detail
