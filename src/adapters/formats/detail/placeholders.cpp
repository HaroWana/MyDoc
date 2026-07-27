#include "detail/placeholders.hpp"

#include <algorithm>
#include <cctype>
#include <unordered_set>

namespace mondoc::adapters::formats::detail {

const std::regex PlaceholderPatterns::kDoubleBrace{
    R"(\{\{\s*([A-Za-z_][A-Za-z0-9_ ]*?)\s*\}\})"};
const std::regex PlaceholderPatterns::kSquareBracket{
    R"(\[([A-Za-z_][A-Za-z0-9_ ]+?)\])"};
const std::regex PlaceholderPatterns::kAngleBracket{
    R"(<([A-Za-z_][A-Za-z0-9_ ]+?)>)"};

std::string normalize(std::string_view raw) {
    std::string s{raw};
    auto first = s.find_first_not_of(" \t\r\n");
    auto last  = s.find_last_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    s = s.substr(first, last - first + 1);
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    std::replace(s.begin(), s.end(), ' ', '_');
    return s;
}

std::vector<std::string> scanPlaceholders(const std::string& text) {
    std::vector<std::string> names;
    std::unordered_set<std::string> seen;

    auto runOne = [&](const std::regex& re) {
        for (auto it = std::sregex_iterator{text.begin(), text.end(), re};
             it != std::sregex_iterator{}; ++it) {
            std::string name = normalize((*it)[1].str());
            if (name.empty() || !seen.insert(name).second) continue;
            names.push_back(std::move(name));
        }
    };
    runOne(PlaceholderPatterns::kDoubleBrace);
    runOne(PlaceholderPatterns::kSquareBracket);
    runOne(PlaceholderPatterns::kAngleBracket);
    return names;
}

}  // namespace mondoc::adapters::formats::detail
