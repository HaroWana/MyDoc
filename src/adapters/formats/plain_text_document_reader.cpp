#include "plain_text_document_reader.hpp"

#include "mondoc/util.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <fstream>
#include <ios>
#include <iterator>
#include <regex>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace mondoc::adapters::formats {

namespace {

constexpr std::uintmax_t kMaxFileBytes = 50ULL * 1024 * 1024;  // 50 MB DoS guard

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

void scanPlaceholders(const std::string& text,
                      std::vector<mondoc::domain::Field>& out,
                      std::unordered_set<std::string>& seen) {
    static const std::regex kDoubleBrace{R"(\{\{\s*([A-Za-z_][A-Za-z0-9_ ]*?)\s*\}\})"};
    static const std::regex kSquareBracket{R"(\[([A-Za-z_][A-Za-z0-9_ ]+?)\])"};
    static const std::regex kAngleBracket{R"(<([A-Za-z_][A-Za-z0-9_ ]+?)>)"};

    auto runOne = [&](const std::regex& re) {
        for (auto it = std::sregex_iterator{text.begin(), text.end(), re};
             it != std::sregex_iterator{}; ++it) {
            std::string name = normalize((*it)[1].str());
            if (name.empty()) continue;
            if (!seen.insert(name).second) continue;
            mondoc::domain::Field f;
            f.id_   = mondoc::FieldId{generateUuid()};
            f.name_ = std::move(name);
            f.type_ = mondoc::domain::FieldType::Text;
            out.push_back(std::move(f));
        }
    };
    runOne(kDoubleBrace);
    runOne(kSquareBracket);
    runOne(kAngleBracket);
}

}  // namespace

mondoc::expected<mondoc::domain::Template, mondoc::Error>
PlainTextDocumentReader::read(const std::filesystem::path& path) {
    const std::string ext = lowercaseExtension(path);
    if (ext != ".txt" && ext != ".md") {
        return mondoc::unexpected(mondoc::Error::invalidArgument(
            "expected .txt or .md extension"));
    }

    std::error_code ec;
    auto fileSize = std::filesystem::file_size(path, ec);
    if (ec) {
        return mondoc::unexpected(mondoc::Error::generic(
            std::string{"cannot stat file: "} + ec.message()));
    }
    if (fileSize > kMaxFileBytes) {
        return mondoc::unexpected(mondoc::Error::generic("file too large"));
    }

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return mondoc::unexpected(mondoc::Error::generic(
            "failed to open file"));
    }
    std::string content{std::istreambuf_iterator<char>{in},
                        std::istreambuf_iterator<char>{}};

    std::vector<mondoc::domain::Field> fields;
    std::unordered_set<std::string> seen;
    scanPlaceholders(content, fields, seen);

    mondoc::domain::Template t;
    t.id_            = mondoc::TemplateId{generateUuid()};
    t.name_          = path.stem().string();
    t.source_format_ = ext.substr(1);
    t.fields_        = std::move(fields);
    t.source_path_   = std::filesystem::absolute(path, ec);
    if (ec) {
        t.source_path_ = path;
    }
    return t;
}

}  // namespace mondoc::adapters::formats
