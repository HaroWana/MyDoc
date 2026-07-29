#pragma once
#include <atomic>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "i_llm_client.hpp"
#include "llm_error.hpp"
#include "mondoc/expected.hpp"

namespace mondoc::adapters::ai {

bool isAcceptableHost(const std::string& url);

class LlmClient : public ILlmClient {
public:
    static mondoc::expected<std::unique_ptr<LlmClient>, LlmError>
    create(std::string host, std::string apiKey, std::string pathPrefix = "/v1");

    ~LlmClient() override = default;

    mondoc::expected<std::string, LlmError>
    chat(const std::string& body, const std::atomic<bool>* cancelled = nullptr) override;

    mondoc::expected<std::vector<std::string>, LlmError>
    listModels(const std::atomic<bool>* cancelled = nullptr) override;

    // Visible for testing: parses {"data":[{"id":"..."},...]}. Entries
    // without a string "id" are skipped; malformed JSON or missing/non-array
    // "data" is badResponse. Result sorted alphabetically.
    static mondoc::expected<std::vector<std::string>, LlmError>
    parseModelsResponse(const std::string& body);

    static LlmError classifyHttpStatus(int status, std::string bodyTrunc);

    // Splits "https://host[:port][/prefix]" into (scheme://host[:port], /prefix).
    // Empty prefix when the URL has no path (or only "/"); trailing slash
    // stripped. Visible for testing.
    static std::pair<std::string, std::string> splitApiUrl(const std::string& url);

    const std::string& host() const noexcept { return host_; }
    const std::string& pathPrefix() const noexcept { return path_prefix_; }

private:
    LlmClient(std::string host, std::string apiKey, std::string pathPrefix);

    std::string host_;
    std::string api_key_;
    std::string path_prefix_;
};

}  // namespace mondoc::adapters::ai
