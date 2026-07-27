#include "composition_root.hpp"

namespace {

using mondoc::adapters::ai::AiFieldDetector;
using mondoc::adapters::ai::AiFillPipeline;
using mondoc::adapters::ai::LlmClient;
using mondoc::adapters::ai::LlmConfig;

static std::unique_ptr<LlmClient> makeClient(const LlmConfig& cfg) {
    if (!cfg.isConfigured()) return nullptr;
    auto result = LlmClient::create(cfg.api_url, cfg.api_key);
    if (!result.has_value()) return nullptr;
    return std::move(result.value());
}

static std::unique_ptr<AiFillPipeline> makePipeline(LlmClient* client, const LlmConfig& cfg) {
    if (!client) return nullptr;
    return std::make_unique<AiFillPipeline>(*client, cfg);
}

static std::unique_ptr<AiFieldDetector> makeDetector(LlmClient* client, const LlmConfig& cfg) {
    if (!client) return nullptr;
    return std::make_unique<AiFieldDetector>(*client, cfg);
}

}  // namespace

namespace mondoc::app {

CompositionRoot::CompositionRoot(mondoc::adapters::storage::SqliteConnection conn,
                                 mondoc::adapters::ai::LlmConfig llmConfig)
    : conn_(std::move(conn)),
      repo_(conn_),
      fill_session_repo_(conn_),
      service_(repo_),
      llm_client_(makeClient(llmConfig)),
      ai_pipeline_(makePipeline(llm_client_.get(), llmConfig)),
      ai_field_detector_(makeDetector(llm_client_.get(), llmConfig)),
      fill_session_service_(fill_session_repo_, repo_, ai_pipeline_.get()),
      config_(std::move(llmConfig)) {}

void CompositionRoot::reconfigureLlm(mondoc::adapters::ai::LlmConfig config) {
    // Destroy detector and pipeline first — both hold raw references to llm_client_.
    ai_field_detector_.reset();
    ai_pipeline_.reset();
    llm_client_.reset();
    llm_client_        = makeClient(config);
    ai_pipeline_       = makePipeline(llm_client_.get(), config);
    ai_field_detector_ = makeDetector(llm_client_.get(), config);
    fill_session_service_.setAiPipeline(ai_pipeline_.get());
    config_ = std::move(config);
}

}  // namespace mondoc::app
