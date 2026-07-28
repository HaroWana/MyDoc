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
    auto st = std::filesystem::status(path, ec);
    if (st.type() == std::filesystem::file_type::not_found) {
        // A genuinely absent file is not an error (FILL-12's "AI disabled
        // gracefully" path) even though libstdc++ sets ec (ENOENT) here too.
        return LlmConfig{};
    }
    if (ec) {
        return mondoc::unexpected(mondoc::Error::generic(
            "cannot stat llm config file: " + ec.message()));
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
            cfg.api_url_ = doc.at("api_url").get<std::string>();
        if (doc.contains("api_key"))
            cfg.api_key_ = doc.at("api_key").get<std::string>();
        if (doc.contains("model"))
            cfg.model_ = doc.at("model").get<std::string>();
    } catch (const nlohmann::json::type_error& e) {
        return mondoc::unexpected(mondoc::Error::generic(
            std::string{"llm config wrong type: "} + e.what()));
    }
    return cfg;
}

mondoc::expected<void, mondoc::Error>
LlmConfig::saveToJson(const std::filesystem::path& path) const {
    nlohmann::json doc;
    doc["api_url"] = api_url_;
    doc["api_key"] = api_key_;
    doc["model"]   = model_;

    std::error_code ec;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), ec);
    }

    auto tmpPath = path;
    tmpPath += ".tmp";

    {
        std::ofstream create(tmpPath, std::ios::binary | std::ios::trunc);
        if (!create) {
            auto u8 = tmpPath.u8string();
            return mondoc::unexpected(mondoc::Error::generic(
                std::string{"cannot create temp config file: "} +
                std::string(reinterpret_cast<const char*>(u8.data()), u8.size())));
        }
    }

    // Restrict permissions before any content (including the API key) is
    // written, so the key is never briefly world/group-readable on disk.
    // On Windows this is a no-op (fs::permissions cannot express ACLs there).
#if !defined(_WIN32)
    {
        std::error_code pec;
        std::filesystem::permissions(tmpPath,
            std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
            std::filesystem::perm_options::replace, pec);
    }
#endif

    std::ofstream out(tmpPath, std::ios::binary | std::ios::trunc);
    if (!out) {
        auto u8 = tmpPath.u8string();
        return mondoc::unexpected(mondoc::Error::generic(
            std::string{"cannot write config file: "} +
            std::string(reinterpret_cast<const char*>(u8.data()), u8.size())));
    }
    out << doc.dump(2);
    if (!out) {
        auto u8 = tmpPath.u8string();
        return mondoc::unexpected(mondoc::Error::generic(
            std::string{"write error on config file: "} +
            std::string(reinterpret_cast<const char*>(u8.data()), u8.size())));
    }
    out.close();

    std::error_code renameEc;
    std::filesystem::rename(tmpPath, path, renameEc);
    if (renameEc) {
        auto u8 = path.u8string();
        return mondoc::unexpected(mondoc::Error::generic(
            std::string{"cannot rename temp config file to: "} +
            std::string(reinterpret_cast<const char*>(u8.data()), u8.size())));
    }
    return {};
}

}  // namespace mondoc::adapters::ai
