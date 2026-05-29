#include "llm_config.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
#include <ios>
#include <iterator>
#include <string>
#include <system_error>

namespace mondoc::adapters::ai {

mondoc::expected<LlmConfig, mondoc::Error>
LlmConfig::loadFromJson(const std::filesystem::path& path) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || ec) {
        return LlmConfig{};
    }

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return mondoc::unexpected(mondoc::Error::generic(
            "cannot open llm config file"));
    }
    std::string body{std::istreambuf_iterator<char>{in},
                     std::istreambuf_iterator<char>{}};
    if (body.empty()) {
        return LlmConfig{};
    }

    nlohmann::json doc;
    try {
        doc = nlohmann::json::parse(body);
    } catch (const nlohmann::json::parse_error& e) {
        return mondoc::unexpected(mondoc::Error::generic(
            std::string{"llm config json parse error: "} + e.what()));
    }

    LlmConfig cfg;
    try {
        if (doc.contains("api_url"))
            cfg.api_url = doc.at("api_url").get<std::string>();
        if (doc.contains("api_key"))
            cfg.api_key = doc.at("api_key").get<std::string>();
        if (doc.contains("model"))
            cfg.model = doc.at("model").get<std::string>();
    } catch (const nlohmann::json::type_error& e) {
        return mondoc::unexpected(mondoc::Error::generic(
            std::string{"llm config wrong type: "} + e.what()));
    }
    return cfg;
}

mondoc::expected<void, mondoc::Error>
LlmConfig::saveToJson(const std::filesystem::path& path) const {
    nlohmann::json doc;
    doc["api_url"] = api_url;
    doc["api_key"] = api_key;
    doc["model"]   = model;

    std::error_code ec;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), ec);
    }

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        auto u8 = path.u8string();
        return mondoc::unexpected(mondoc::Error::generic(
            std::string{"cannot write config file: "} +
            std::string(reinterpret_cast<const char*>(u8.data()), u8.size())));
    }
    out << doc.dump(2);
    if (!out) {
        auto u8 = path.u8string();
        return mondoc::unexpected(mondoc::Error::generic(
            std::string{"write error on config file: "} +
            std::string(reinterpret_cast<const char*>(u8.data()), u8.size())));
    }
    return {};
}

}  // namespace mondoc::adapters::ai
