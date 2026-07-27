#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <string_view>

#include "llm_error.hpp"
#include "mondoc/expected.hpp"

namespace mondoc::adapters::ai::detail {

// Extracts the inner structured-output JSON from a chat completion envelope:
//   {"choices":[{"message":{"content":"<inner-json-string>"}}]}
// Returns LlmError::BadResponse on any parse failure or missing field.
mondoc::expected<nlohmann::json, LlmError>
parseChatCompletionContent(const std::string& body);

nlohmann::json buildJsonSchemaResponseFormat(std::string_view name,
                                             std::string_view schemaLiteral);

}  // namespace mondoc::adapters::ai::detail
