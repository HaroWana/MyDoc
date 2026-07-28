#pragma once
#include <atomic>
#include <memory>
#include <string>

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

    static LlmError classifyHttpStatus(int status, std::string bodyTrunc);

private:
    LlmClient(std::string host, std::string apiKey, std::string pathPrefix);

    std::string host_;
    std::string api_key_;
    std::string path_prefix_;
};

}  // namespace mondoc::adapters::ai
