#pragma once
#include <string>

#include "i_llm_client.hpp"
#include "llm_error.hpp"
#include "mondoc/expected.hpp"

namespace mondoc::adapters::ai {

class LlmClient : public ILlmClient {
public:
    LlmClient(std::string host, std::string apiKey,
              std::string pathPrefix = "/v1");
    ~LlmClient() override = default;

    mondoc::expected<std::string, LlmError>
    chat(const std::string& body) override;

    static LlmError classifyHttpStatus(int status, std::string bodyTrunc);

    static bool isAcceptableHost(const std::string& host);

private:
    std::string host_;
    std::string apiKey_;
    std::string pathPrefix_;
};

}  // namespace mondoc::adapters::ai
