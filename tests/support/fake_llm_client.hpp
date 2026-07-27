#pragma once

#include <atomic>
#include <functional>
#include <queue>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "i_llm_client.hpp"
#include "llm_error.hpp"
#include "mondoc/expected.hpp"

namespace mondoc::tests_support {

class FakeLlmClient : public mondoc::adapters::ai::ILlmClient {
public:
    std::queue<mondoc::expected<std::string, mondoc::adapters::ai::LlmError>> responses_;
    std::vector<std::string> chatCalls_;
    std::function<void()> onAfterCall_;

    void enqueueOk(std::string body) { responses_.emplace(std::move(body)); }
    void enqueueErr(mondoc::adapters::ai::LlmError e) {
        responses_.emplace(mondoc::unexpected<mondoc::adapters::ai::LlmError>(std::move(e)));
    }

    mondoc::expected<std::string, mondoc::adapters::ai::LlmError>
    chat(const std::string& body, const std::atomic<bool>* /*cancelled*/) override {
        chatCalls_.push_back(body);
        if (responses_.empty()) {
            if (onAfterCall_) onAfterCall_();
            return mondoc::unexpected<mondoc::adapters::ai::LlmError>(
                mondoc::adapters::ai::LlmError::unreachable("fake exhausted"));
        }
        auto r = std::move(responses_.front());
        responses_.pop();
        if (onAfterCall_) onAfterCall_();
        return r;
    }
};

// Wraps already-serialized JSON content (e.g. a raw string body under
// construction) into a minimal chat-completion envelope.
inline std::string makeChatCompletion(const std::string& contentJson) {
    nlohmann::json envelope = {
        {"choices", nlohmann::json::array({
            nlohmann::json{{"message", nlohmann::json{{"content", contentJson}}}}
        })}
    };
    return envelope.dump();
}

// Wraps a JSON value into a minimal chat-completion envelope, serializing
// it into the message content first.
inline std::string makeChatCompletion(const nlohmann::json& contentJson) {
    return makeChatCompletion(contentJson.dump());
}

}  // namespace mondoc::tests_support
