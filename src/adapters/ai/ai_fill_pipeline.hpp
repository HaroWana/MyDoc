#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include "domain/confidence.hpp"
#include "domain/fill.hpp"
#include "domain/template.hpp"
#include "i_llm_client.hpp"
#include "llm_config.hpp"
#include "llm_error.hpp"
#include "mondoc/expected.hpp"
#include "mondoc/id.hpp"

namespace mondoc::adapters::ai {

mondoc::domain::Confidence parseConfidence(std::string_view s) noexcept;


struct AiFillSourceDoc {
    mondoc::SourceDocId id_;
    std::string title_;
    std::string text_;
};

struct RunInput {
    const mondoc::domain::Template* tpl_ = nullptr;
    std::vector<AiFillSourceDoc> sources_;
    std::string free_form_text_;
};

struct RefineInput {
    const mondoc::domain::Template* tpl_ = nullptr;
    std::vector<AiFillSourceDoc> sources_;
    std::vector<mondoc::domain::Fill> current_fills_;
    std::string user_message_;
};

struct ExtractedFact {
    std::size_t source_index_ = 0;
    std::int64_t char_start_  = 0;
    std::int64_t char_end_    = 0;
    std::string excerpt_;
    std::string summary_;
};

class AiFillPipeline {
public:
    AiFillPipeline(ILlmClient& client, LlmConfig config) noexcept;

    // Synchronous; dispatched from a QThread by the UI layer. Checks
    // `cancelled` after every chat() return (Pass 1 and Pass 2).
    mondoc::expected<std::vector<mondoc::domain::Fill>, LlmError>
    run(const RunInput& input, const std::atomic<bool>& cancelled);

    // Single LLM call: refine a previously-filled form. Returns only the
    // fields that changed (caller merges). `cancelled` may be null when the
    // caller has no cancellation token; if non-null, checked after the
    // chat() return.
    mondoc::expected<std::vector<mondoc::domain::Fill>, LlmError>
    refine(const RefineInput& input, const std::atomic<bool>* cancelled);

    // Visible for testing: validates LLM-returned offsets against source
    // text, drops facts whose excerpt does not match (Pitfall 1). Accepts at
    // most 200 facts (deterministic prefix), matching kMaxProposals in
    // AiFieldDetector (SAI-6).
    static std::vector<ExtractedFact>
    validatePass1Facts(const std::string& jsonContent,
                       const std::vector<AiFillSourceDoc>& sources);

    // Visible for testing (SAI-19).
    static std::string normalizeDateValue(const std::string& v);
    static std::string normalizeNumberValue(const std::string& v);

private:
    ILlmClient& client_;
    LlmConfig config_;
};

}  // namespace mondoc::adapters::ai
