#include "composition_root.hpp"

namespace {

using mondoc::adapters::ai::AiFillPipeline;
using mondoc::adapters::ai::LlmClient;
using mondoc::adapters::ai::LlmConfig;

static std::unique_ptr<LlmClient> makeClient(const LlmConfig& cfg) {
    if (!cfg.isConfigured()) return nullptr;
    return std::make_unique<LlmClient>(cfg.api_url, cfg.api_key);
}

static std::unique_ptr<AiFillPipeline> makePipeline(LlmClient* client, const LlmConfig& cfg) {
    if (!client) return nullptr;
    return std::make_unique<AiFillPipeline>(*client, cfg);
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
      fill_session_service_(fill_session_repo_, repo_, ai_pipeline_.get()) {}

}  // namespace mondoc::app
