#include "ai_fill_pipeline.hpp"

#include <nlohmann/json.hpp>

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <regex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

#include "pass1_prompt.hpp"
#include "pass2_prompt.hpp"
#include "refine_prompt.hpp"

namespace mondoc::adapters::ai {

namespace {

std::string normalizeDateValue(const std::string& v) {
    std::smatch m;
    std::regex re(R"((\d{4})-(\d{1,2})-(\d{1,2}))");
    if (std::regex_search(v, m, re)) {
        char buf[11];
        std::snprintf(buf, sizeof(buf), "%s-%02d-%02d",
                      m[1].str().c_str(),
                      std::stoi(m[2].str()),
                      std::stoi(m[3].str()));
        return buf;
    }
    return v;
}

std::string normalizeNumberValue(const std::string& v) {
    std::string digits;
    for (char c : v) {
        if (std::isdigit(static_cast<unsigned char>(c)) || c == '-' || c == '.') {
            digits += c;
        }
    }
    return digits.empty() ? v : digits;
}

std::string normalizeForFieldType(mondoc::domain::FieldType t, const std::string& v) {
    using mondoc::domain::FieldType;
    if (t == FieldType::Date)   return normalizeDateValue(v);
    if (t == FieldType::Number) return normalizeNumberValue(v);
    return v;
}

// Extracts the inner structured-output JSON from a chat completion envelope:
//   {"choices":[{"message":{"content":"<inner-json-string>"}}]}
// Returns LlmError::BadResponse on any parse failure or missing field.
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

}  // namespace

mondoc::domain::Confidence parseConfidence(std::string_view s) noexcept {
    if (s == "high")   return mondoc::domain::Confidence::High;
    if (s == "medium") return mondoc::domain::Confidence::Medium;
    return mondoc::domain::Confidence::Low;
}

AiFillPipeline::AiFillPipeline(ILlmClient& client, LlmConfig config) noexcept
    : client_(client), config_(std::move(config)) {}

std::vector<ExtractedFact>
AiFillPipeline::validatePass1Facts(const std::string& jsonContent,
                                   const std::vector<AiFillSourceDoc>& sources) {
    std::vector<ExtractedFact> accepted;
    try {
        auto outer = nlohmann::json::parse(jsonContent);
        if (!outer.contains("choices") || !outer["choices"].is_array() ||
            outer["choices"].empty()) {
            return accepted;
        }
        const auto& message = outer["choices"][0];
        if (!message.contains("message") ||
            !message["message"].contains("content") ||
            !message["message"]["content"].is_string()) {
            return accepted;
        }
        auto inner = nlohmann::json::parse(message["message"]["content"].get<std::string>());
        if (!inner.contains("facts") || !inner["facts"].is_array()) {
            return accepted;
        }

        for (const auto& item : inner["facts"]) {
            auto source_index = item.value("source_index", static_cast<std::int64_t>(-1));
            auto char_start   = item.value("char_start",   static_cast<std::int64_t>(-1));
            auto char_end     = item.value("char_end",     static_cast<std::int64_t>(-1));
            auto excerpt      = item.value("excerpt",      std::string{});
            auto summary      = item.value("summary",      std::string{});

            if (source_index < 0 ||
                static_cast<std::size_t>(source_index) >= sources.size()) {
                continue;
            }
            const auto& sourceText = sources[static_cast<std::size_t>(source_index)].text_;

            std::int64_t start = char_start;
            std::int64_t end   = char_end;
            const bool offsetsBoundsOk =
                start >= 0 && end > start &&
                static_cast<std::size_t>(end) <= sourceText.size();

            bool matched = false;
            if (offsetsBoundsOk) {
                if (sourceText.substr(static_cast<std::size_t>(start),
                                      static_cast<std::size_t>(end - start)) == excerpt) {
                    matched = true;
                }
            }
            if (!matched) {
                auto pos = sourceText.find(excerpt);
                if (pos == std::string::npos || excerpt.empty()) {
                    continue;
                }
                start = static_cast<std::int64_t>(pos);
                end   = static_cast<std::int64_t>(pos + excerpt.size());
            }

            ExtractedFact f;
            f.source_index_ = static_cast<std::size_t>(source_index);
            f.char_start_   = start;
            f.char_end_     = end;
            f.excerpt_      = std::move(excerpt);
            f.summary_      = std::move(summary);
            accepted.push_back(std::move(f));
        }
    } catch (const nlohmann::json::exception&) {
        return {};
    }
    return accepted;
}

mondoc::expected<std::vector<mondoc::domain::Fill>, LlmError>
AiFillPipeline::run(const RunInput& input, const std::atomic<bool>& cancelled) {
    if (input.tpl_ == nullptr) {
        return mondoc::unexpected<LlmError>(LlmError::badResponse("missing template"));
    }

    nlohmann::json pass1Body = {
        {"model", config_.model},
        {"messages", nlohmann::json::array({
            nlohmann::json{{"role","system"},{"content", std::string(kPass1SystemPrompt)}},
            nlohmann::json{{"role","user"},  {"content", buildPass1UserPrompt(input.sources_, input.free_form_text_)}},
        })},
        {"response_format", buildJsonSchemaResponseFormat("pass1", kPass1JsonSchema)},
        {"temperature", 0.0},
        {"stream", false},
    };

    auto pass1Resp = client_.chat(pass1Body.dump());
    if (!pass1Resp) return mondoc::unexpected<LlmError>(pass1Resp.error());

    auto facts = validatePass1Facts(*pass1Resp, input.sources_);

    if (cancelled.load()) {
        return mondoc::unexpected<LlmError>(LlmError::cancelled());
    }

    nlohmann::json pass2Body = {
        {"model", config_.model},
        {"messages", nlohmann::json::array({
            nlohmann::json{{"role","system"},{"content", std::string(kPass2SystemPrompt)}},
            nlohmann::json{{"role","user"},  {"content", buildPass2UserPrompt(*input.tpl_, facts)}},
        })},
        {"response_format", buildJsonSchemaResponseFormat("pass2", kPass2JsonSchema)},
        {"temperature", 0.0},
        {"stream", false},
    };

    auto pass2Resp = client_.chat(pass2Body.dump());
    if (!pass2Resp) return mondoc::unexpected<LlmError>(pass2Resp.error());

    auto contentOrErr = parseChatCompletionContent(*pass2Resp);
    if (!contentOrErr) return mondoc::unexpected<LlmError>(contentOrErr.error());
    auto content = *contentOrErr;

    std::unordered_map<std::string, nlohmann::json> fillsByFieldId;
    if (content.contains("fills") && content["fills"].is_array()) {
        for (const auto& f : content["fills"]) {
            fillsByFieldId[f.value("field_id", std::string{})] = f;
        }
    }

    std::vector<mondoc::domain::Fill> result;
    result.reserve(input.tpl_->fields_.size());
    for (const auto& field : input.tpl_->fields_) {
        mondoc::domain::Fill fill;
        fill.field_id_ = field.id_;

        auto it = fillsByFieldId.find(field.id_.value());
        if (it == fillsByFieldId.end()) {
            fill.current_value_ = "";
            fill.confidence_    = mondoc::domain::Confidence::Low;
            result.push_back(std::move(fill));
            continue;
        }

        const auto& entry = it->second;
        auto rawValue = entry.value("value", std::string{});
        fill.current_value_ = normalizeForFieldType(field.type_, rawValue);
        fill.confidence_    = parseConfidence(entry.value("confidence", std::string{}));

        auto factIndex = entry.value("fact_index", static_cast<std::int64_t>(-1));
        if (factIndex >= 0 && static_cast<std::size_t>(factIndex) < facts.size()) {
            const auto& fact = facts[static_cast<std::size_t>(factIndex)];
            if (fact.source_index_ < input.sources_.size()) {
                mondoc::domain::SourceRef ref;
                ref.source_id_ = input.sources_[fact.source_index_].id_;
                ref.range_.begin_ = fact.char_start_;
                ref.range_.end_   = fact.char_end_;
                ref.excerpt_      = fact.excerpt_;
                fill.source_refs_.push_back(std::move(ref));
            }
        }

        result.push_back(std::move(fill));
    }
    return result;
}

mondoc::expected<std::vector<mondoc::domain::Fill>, LlmError>
AiFillPipeline::refine(const RefineInput& input) {
    if (input.tpl_ == nullptr || input.user_message_.empty()) {
        return mondoc::unexpected<LlmError>(
            LlmError::badResponse("missing template or message"));
    }

    nlohmann::json body = {
        {"model", config_.model},
        {"messages", nlohmann::json::array({
            nlohmann::json{{"role","system"},{"content", std::string(kRefineSystemPrompt)}},
            nlohmann::json{{"role","user"},  {"content",
                buildRefineUserPrompt(*input.tpl_, input.sources_,
                                      input.current_fills_, input.user_message_)}},
        })},
        {"response_format", buildJsonSchemaResponseFormat("refine", kRefineJsonSchema)},
        {"temperature", 0.0},
        {"stream", false},
    };

    auto resp = client_.chat(body.dump());
    if (!resp) return mondoc::unexpected<LlmError>(resp.error());

    auto contentOrErr = parseChatCompletionContent(*resp);
    if (!contentOrErr) return mondoc::unexpected<LlmError>(contentOrErr.error());
    auto content = *contentOrErr;

    std::vector<mondoc::domain::Fill> updates;
    if (!content.contains("updates") || !content["updates"].is_array()) {
        return updates;
    }

    std::unordered_map<std::string, mondoc::domain::FieldType> typeByFieldId;
    for (const auto& f : input.tpl_->fields_) {
        typeByFieldId.emplace(f.id_.value(), f.type_);
    }

    for (const auto& entry : content["updates"]) {
        auto fieldId   = entry.value("field_id",   std::string{});
        auto rawValue  = entry.value("value",      std::string{});
        auto confidence = entry.value("confidence", std::string{});

        mondoc::domain::Fill fill;
        fill.field_id_ = mondoc::FieldId{fieldId};
        auto it = typeByFieldId.find(fieldId);
        fill.current_value_ = it != typeByFieldId.end()
            ? normalizeForFieldType(it->second, rawValue)
            : rawValue;
        fill.confidence_ = parseConfidence(confidence);
        updates.push_back(std::move(fill));
    }
    return updates;
}

}  // namespace mondoc::adapters::ai
