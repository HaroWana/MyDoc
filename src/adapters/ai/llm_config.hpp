#pragma once
#include <filesystem>
#include <string>
#include "mondoc/error.hpp"
#include "mondoc/expected.hpp"

namespace mondoc::adapters::ai {

struct LlmConfig {
    std::string api_url;
    std::string api_key;
    std::string model;

    bool isConfigured() const noexcept {
        return !api_url.empty() && !api_key.empty();
    }

    // Returns a default-constructed (unconfigured) LlmConfig when the file
    // does not exist or is empty — FILL-12's "AI disabled gracefully" path,
    // not an error. Returns Error only on malformed JSON or unreadable file.
    static mondoc::expected<LlmConfig, mondoc::Error>
    loadFromJson(const std::filesystem::path& path);

    mondoc::expected<void, mondoc::Error>
    saveToJson(const std::filesystem::path& path) const;
};

}  // namespace mondoc::adapters::ai
