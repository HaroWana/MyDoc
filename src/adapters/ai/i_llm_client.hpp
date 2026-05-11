#pragma once
#include <string>
#include "llm_error.hpp"
#include "mondoc/expected.hpp"

namespace mondoc::adapters::ai {

class ILlmClient {
public:
    virtual ~ILlmClient() = default;

    // body is the already-serialized JSON of {model, messages, response_format, ...}.
    // Returns the raw response body string from /v1/chat/completions on success.
    virtual mondoc::expected<std::string, LlmError>
        chat(const std::string& body) = 0;
};

}  // namespace mondoc::adapters::ai
