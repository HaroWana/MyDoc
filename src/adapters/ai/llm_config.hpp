#pragma once
#include <filesystem>
#include <string>
#include "mondoc/error.hpp"
#include "mondoc/expected.hpp"

namespace mondoc::adapters::ai {

struct LlmConfig {
    std::string api_url_;
    std::string api_key_;
    std::string model_;

    bool isConfigured() const noexcept {
        return !api_url_.empty() && !api_key_.empty();
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
