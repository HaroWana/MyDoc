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

namespace {
// One combined alternation over the same three token charsets as
// PlaceholderPatterns, so a single left-to-right scan finds every
// placeholder without ever re-scanning already-substituted output.
const std::regex kCombinedPattern{
    R"(\{\{\s*([A-Za-z_][A-Za-z0-9_ ]*?)\s*\}\}|\[([A-Za-z_][A-Za-z0-9_ ]+?)\]|<([A-Za-z_][A-Za-z0-9_ ]+?)>)"};
}  // namespace

std::string substituteAll(const std::string& original,
                          const std::unordered_map<std::string, std::string>& fills) {
    std::string out;
    out.reserve(original.size());
    auto cursor = original.cbegin();
    for (auto it = std::sregex_iterator{original.cbegin(), original.cend(), kCombinedPattern};
         it != std::sregex_iterator{}; ++it) {
        out.append(cursor, original.cbegin() + it->position());
        std::string name;
        for (int group = 1; group <= 3; ++group) {
            if ((*it)[group].matched) {
                name = normalize((*it)[group].str());
                break;
            }
        }
        auto v = fills.find(name);
        out += (v != fills.end() ? v->second : it->str());
        cursor = original.cbegin() + it->position() + it->length();
    }
    out.append(cursor, original.cend());
    return out;
}

}  // namespace mondoc::adapters::formats::detail
