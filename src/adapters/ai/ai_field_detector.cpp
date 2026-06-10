#include "ai_field_detector.hpp"

#include <nlohmann/json.hpp>
#include <string>
#include <string_view>

#include "field_detect_prompt.hpp"

namespace mondoc::adapters::ai {

namespace {

// Duplicate of the same helper in ai_fill_pipeline.cpp (anonymous namespace, no external linkage).
// If extracted to ai_utils.hpp in future, remove both copies and update the comment there.
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

// Duplicate of the same helper in ai_fill_pipeline.cpp (anonymous namespace, no external linkage).
// If extracted to ai_utils.hpp in future, remove both copies and update the comment there.
nlohmann::json buildJsonSchemaResponseFormat(std::string_view name,
                                             std::string_view schemaLiteral) {
    return nlohmann::json{
        {"type", "json_schema"},
        {"json_schema", {
            {"name",   std::string(name)},
            {"strict", true},
            {"schema", nlohmann::json::parse(schemaLiteral)},
        }},
    };
}

mondoc::domain::FieldType parseFieldType(const std::string& s) {
    using mondoc::domain::FieldType;
    if (s == "paragraph") return FieldType::Paragraph;
    if (s == "number")    return FieldType::Number;
    if (s == "date")      return FieldType::Date;
    if (s == "checkbox")  return FieldType::Checkbox;
    if (s == "dropdown")  return FieldType::Dropdown;
    return FieldType::Text;
}

DetectionResult parseDetectionResult(const nlohmann::json& content) {
    DetectionResult result;
    constexpr std::size_t kMaxProposals = 200;

    if (content.contains("new_fields") && content["new_fields"].is_array()) {
        for (const auto& item : content["new_fields"]) {
            if (result.new_fields.size() >= kMaxProposals) break;
            if (!item.contains("name") || !item.contains("type")) continue;
            auto name = item["name"].get<std::string>();
            if (name.empty()) continue;
            mondoc::domain::Field f;
            f.name_   = std::move(name);
            f.type_   = parseFieldType(item["type"].get<std::string>());
            f.origin_ = mondoc::domain::FieldOrigin::Ai;
            result.new_fields.push_back(std::move(f));
        }
    }

    if (content.contains("improvements") && content["improvements"].is_array()) {
        for (const auto& item : content["improvements"]) {
            if (result.improvements.size() >= kMaxProposals) break;
            if (!item.contains("field_name") ||
                !item.contains("suggested_name") ||
                !item.contains("suggested_type")) continue;
            FieldImprovement imp;
            imp.field_name     = item["field_name"].get<std::string>();
            imp.suggested_name = item["suggested_name"].get<std::string>();
            imp.suggested_type = item["suggested_type"].get<std::string>();
            result.improvements.push_back(std::move(imp));
        }
    }

    return result;
}

}  // namespace

AiFieldDetector::AiFieldDetector(ILlmClient& client, LlmConfig config) noexcept
    : client_(client), config_(std::move(config)) {}

mondoc::expected<DetectionResult, LlmError>
AiFieldDetector::detect(const std::string& documentText,
                        const std::vector<mondoc::domain::Field>& existingFields,
                        const std::atomic<bool>& cancelled)
{
    if (cancelled.load()) {
        return mondoc::unexpected<LlmError>(LlmError::cancelled());
    }

    nlohmann::json body = {
        {"model", config_.model},
        {"messages", nlohmann::json::array({
            nlohmann::json{{"role", "system"}, {"content", std::string(kDetectSystemPrompt)}},
            nlohmann::json{{"role", "user"},   {"content", buildDetectUserPrompt(documentText, existingFields)}},
        })},
        {"response_format", buildJsonSchemaResponseFormat("field_detection", kDetectJsonSchema)},
        {"temperature", 0.0},
        {"stream", false},
    };

    auto resp = client_.chat(body.dump());
    if (!resp) return mondoc::unexpected<LlmError>(resp.error());

    if (cancelled.load()) {
        return mondoc::unexpected<LlmError>(LlmError::cancelled());
    }

    auto contentOrErr = parseChatCompletionContent(*resp);
    if (!contentOrErr) return mondoc::unexpected<LlmError>(contentOrErr.error());

    return parseDetectionResult(*contentOrErr);
}

}  // namespace mondoc::adapters::ai
