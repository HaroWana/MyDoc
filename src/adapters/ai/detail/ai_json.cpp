#include "detail/ai_json.hpp"

namespace mondoc::adapters::ai::detail {

mondoc::expected<nlohmann::json, LlmError>
parseChatCompletionContent(const std::string& body) {
    try {
        auto outer = nlohmann::json::parse(body);
        if (!outer.contains("choices") || !outer["choices"].is_array() ||
            outer["choices"].empty()) {
            return mondoc::unexpected<LlmError>(LlmError::badResponse("missing choices"));
        }
        const auto& msg = outer["choices"][0];
        if (!msg.contains("message") ||
            !msg["message"].contains("content") ||
            !msg["message"]["content"].is_string()) {
            return mondoc::unexpected<LlmError>(LlmError::badResponse("missing content"));
        }
        return nlohmann::json::parse(msg["message"]["content"].get<std::string>());
    } catch (const nlohmann::json::exception& e) {
        return mondoc::unexpected<LlmError>(LlmError::badResponse(e.what()));
    }
}

nlohmann::json buildJsonSchemaResponseFormat(std::string_view name,
                                             std::string_view schemaLiteral) {
    return nlohmann::json{
        {"type", "json_schema"},
        {"json_schema", {
            {"name", std::string(name)},
            {"strict", true},
            {"schema", nlohmann::json::parse(schemaLiteral)},
        }},
    };
}

}  // namespace mondoc::adapters::ai::detail
