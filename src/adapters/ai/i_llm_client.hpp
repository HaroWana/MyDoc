#pragma once
#include <atomic>
#include <string>
#include "llm_error.hpp"
#include "mondoc/expected.hpp"

namespace mondoc::adapters::ai {

class ILlmClient {
public:
    virtual ~ILlmClient() = default;

    // body is the already-serialized JSON of {model, messages, response_format, ...}.
    // Returns the raw response body string from /v1/chat/completions on success.
    // `cancelled`, if non-null, is polled while the response body is being
    // received so an in-flight request can be aborted mid-stream (SAI-2).
    virtual mondoc::expected<std::string, LlmError>
        chat(const std::string& body, const std::atomic<bool>* cancelled = nullptr) = 0;
};

}  // namespace mondoc::adapters::ai
