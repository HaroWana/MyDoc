#include "text_document_writer.hpp"

#include "detail/placeholders.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <regex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace mondoc::adapters::formats {

namespace {

constexpr std::uintmax_t kMaxFileBytes = 50ULL * 1024 * 1024;

using detail::normalize;

std::unordered_map<std::string, std::string>
fillsByName(const mondoc::domain::Template& tpl,
            const std::vector<mondoc::domain::Fill>& fills) {
    std::unordered_map<std::string, const mondoc::domain::Field*> idToField;
    for (const auto& f : tpl.fields_) idToField.emplace(f.id_.value(), &f);

    std::unordered_map<std::string, std::string> out;
    for (const auto& fill : fills) {
        auto it = idToField.find(fill.field_id_.value());
        if (it != idToField.end()) {
            out[normalize(it->second->name_)] = fill.current_value_;
        }
    }
    return out;
}

std::string substituteOne(std::string text, const std::regex& re,
                          const std::unordered_map<std::string, std::string>& byName) {
    std::string out;
    out.reserve(text.size());
    auto cursor = text.cbegin();
    for (auto it = std::sregex_iterator{text.cbegin(), text.cend(), re};
         it != std::sregex_iterator{}; ++it) {
        out.append(cursor, text.cbegin() + it->position());
        const std::string key = normalize((*it)[1].str());
        auto v = byName.find(key);
        out += (v != byName.end() ? v->second : it->str());
        cursor = text.cbegin() + it->position() + it->length();
    }
    out.append(cursor, text.cend());
    return out;
}

}  // namespace

mondoc::expected<void, mondoc::Error>
TextDocumentWriter::write(const mondoc::domain::Template& tpl,
                          const std::vector<mondoc::domain::Fill>& fills,
                          const std::filesystem::path& dest) {
    if (tpl.source_path_.empty()) {
        return mondoc::unexpected(mondoc::Error::invalidArgument(
            "template source_path_ is empty"));
    }

    std::error_code ec;
    auto fileSize = std::filesystem::file_size(tpl.source_path_, ec);
    if (ec) {
        return mondoc::unexpected(mondoc::Error::generic(
            std::string{"cannot stat template: "} + ec.message()));
    }
    if (fileSize > kMaxFileBytes) {
        return mondoc::unexpected(mondoc::Error::generic("template too large"));
    }

    std::ifstream in(tpl.source_path_, std::ios::binary);
    if (!in) {
        return mondoc::unexpected(mondoc::Error::generic(
            "failed to open template"));
    }
    std::string content{std::istreambuf_iterator<char>{in},
                        std::istreambuf_iterator<char>{}};

    const auto byName = fillsByName(tpl, fills);
    content = substituteOne(std::move(content), detail::PlaceholderPatterns::kDoubleBrace,   byName);
    content = substituteOne(std::move(content), detail::PlaceholderPatterns::kSquareBracket, byName);
    content = substituteOne(std::move(content), detail::PlaceholderPatterns::kAngleBracket,  byName);

    std::ofstream of(dest, std::ios::binary | std::ios::trunc);
    if (!of) {
        return mondoc::unexpected(mondoc::Error::generic(
            "failed to open dest for writing"));
    }
    of << content;
    if (!of) {
        return mondoc::unexpected(mondoc::Error::generic("write failed"));
    }
    return {};
}

}  // namespace mondoc::adapters::formats
