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

#include "detail/ai_json.hpp"
#include "pass1_prompt.hpp"
#include "pass2_prompt.hpp"
#include "refine_prompt.hpp"

namespace mondoc::adapters::ai {

namespace {

constexpr std::size_t kMaxFacts = 200;

std::string normalizeForFieldType(mondoc::domain::FieldType t, const std::string& v) {
    using mondoc::domain::FieldType;
    if (t == FieldType::Date)   return AiFillPipeline::normalizeDateValue(v);
    if (t == FieldType::Number) return AiFillPipeline::normalizeNumberValue(v);
    return v;
}

}  // namespace

mondoc::domain::Confidence parseConfidence(std::string_view s) noexcept {
    if (s == "high")   return mondoc::domain::Confidence::High;
    if (s == "medium") return mondoc::domain::Confidence::Medium;
    return mondoc::domain::Confidence::Low;
}

AiFillPipeline::AiFillPipeline(ILlmClient& client, LlmConfig config) noexcept
    : client_(client), config_(std::move(config)) {}

std::string AiFillPipeline::normalizeDateValue(const std::string& v) {
    static const std::regex re(R"((\d{4})-(\d{1,2})-(\d{1,2}))");
    std::smatch m;
    if (std::regex_search(v, m, re)) {
        const int month = std::stoi(m[2].str());
        const int day   = std::stoi(m[3].str());
        if (month < 1 || month > 12 || day < 1 || day > 31) {
            return v;
        }
        char buf[11];
        std::snprintf(buf, sizeof(buf), "%s-%02d-%02d", m[1].str().c_str(), month, day);
        return buf;
    }
    return v;
}

std::string AiFillPipeline::normalizeNumberValue(const std::string& v) {
    std::string out;
    bool seenMinus = false;
    bool seenDot   = false;
    for (char c : v) {
        if (std::isdigit(static_cast<unsigned char>(c))) {
            out += c;
        } else if (c == '-' && out.empty() && !seenMinus) {
            out += c;
            seenMinus = true;
        } else if (c == '.' && !seenDot) {
            out += c;
            seenDot = true;
        }
    }
    return out.empty() ? v : out;
}

std::vector<AiExtractedFact>
AiFillPipeline::validatePass1Facts(const std::string& jsonContent,
                                   const std::vector<AiSourceDoc>& sources) {
    std::vector<AiExtractedFact> accepted;
    auto contentOrErr = detail::parseChatCompletionContent(jsonContent);
    if (!contentOrErr) return accepted;
    const auto& inner = *contentOrErr;
    try {
        if (!inner.contains("facts") || !inner["facts"].is_array()) {
            return accepted;
        }

        for (const auto& item : inner["facts"]) {
            if (accepted.size() >= kMaxFacts) break;
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

            AiExtractedFact f;
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
        {"response_format", detail::buildJsonSchemaResponseFormat("pass1", kPass1JsonSchema)},
        {"temperature", 0.0},
        {"stream", false},
    };

    auto pass1Resp = client_.chat(pass1Body.dump(), &cancelled);
    if (!pass1Resp) return mondoc::unexpected<LlmError>(pass1Resp.error());
    if (cancelled.load()) {
        return mondoc::unexpected<LlmError>(LlmError::cancelled());
    }

    auto facts = validatePass1Facts(*pass1Resp, input.sources_);

    nlohmann::json pass2Body = {
        {"model", config_.model},
        {"messages", nlohmann::json::array({
            nlohmann::json{{"role","system"},{"content", std::string(kPass2SystemPrompt)}},
            nlohmann::json{{"role","user"},  {"content", buildPass2UserPrompt(*input.tpl_, facts)}},
        })},
        {"response_format", detail::buildJsonSchemaResponseFormat("pass2", kPass2JsonSchema)},
        {"temperature", 0.0},
        {"stream", false},
    };

    auto pass2Resp = client_.chat(pass2Body.dump(), &cancelled);
    if (!pass2Resp) return mondoc::unexpected<LlmError>(pass2Resp.error());
    if (cancelled.load()) {
        return mondoc::unexpected<LlmError>(LlmError::cancelled());
    }

    auto contentOrErr = detail::parseChatCompletionContent(*pass2Resp);
    if (!contentOrErr) return mondoc::unexpected<LlmError>(contentOrErr.error());
    auto content = *contentOrErr;

    // SAI-4: a schema-ignoring server can send non-string field_id/value; any
    // nlohmann type_error while mapping the response is reported as
    // LlmError::BadResponse instead of propagating out of this worker-thread call.
    try {
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
    } catch (const nlohmann::json::exception& e) {
        return mondoc::unexpected<LlmError>(LlmError::badResponse(e.what()));
    }
}

mondoc::expected<std::vector<mondoc::domain::Fill>, LlmError>
AiFillPipeline::refine(const RefineInput& input, const std::atomic<bool>* cancelled) {
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
                                      input.current_fills_,
                                      input.last_pass1_facts_,
                                      input.user_message_)}},
        })},
        {"response_format", detail::buildJsonSchemaResponseFormat("refine", kRefineJsonSchema)},
        {"temperature", 0.0},
        {"stream", false},
    };

    auto resp = client_.chat(body.dump(), cancelled);
    if (!resp) return mondoc::unexpected<LlmError>(resp.error());
    if (cancelled != nullptr && cancelled->load()) {
        return mondoc::unexpected<LlmError>(LlmError::cancelled());
    }

    auto contentOrErr = detail::parseChatCompletionContent(*resp);
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

    // SAI-4: see run() above for rationale.
    try {
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
    } catch (const nlohmann::json::exception& e) {
        return mondoc::unexpected<LlmError>(LlmError::badResponse(e.what()));
    }
    return updates;
}

}  // namespace mondoc::adapters::ai
