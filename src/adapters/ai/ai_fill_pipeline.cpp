#include "ai_fill_pipeline.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>
#include <utility>

namespace mondoc::adapters::ai {

AiFillPipeline::AiFillPipeline(ILlmClient& client, LlmConfig config) noexcept
    : client_(client), config_(std::move(config)) {}

// Validates the LLM's Pass 1 JSON response. Real OpenAI responses are wrapped
// as {"choices":[{"message":{"content":"<inner-json-string>"}}]}; the structured
// output is encoded as a string inside `content`. Two parses required:
//   1. Outer parse of the full response body → choices[0].message.content (string).
//   2. Inner parse of that string → facts[] array.
// For each fact, applies the three-tier algorithm from RESEARCH Pitfall 1:
// exact-match by offsets → find() fallback (excerpt search) → drop.
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

}  // namespace mondoc::adapters::ai
