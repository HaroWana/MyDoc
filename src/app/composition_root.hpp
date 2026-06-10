#pragma once

#include <memory>
#include <utility>

#include "ai_field_detector.hpp"
#include "ai_fill_pipeline.hpp"
#include "fill_session_service.hpp"
#include "llm_client.hpp"
#include "llm_config.hpp"
#include "sqlite_connection.hpp"
#include "sqlite_fill_session_repository.hpp"
#include "sqlite_template_repository.hpp"
#include "template_service.hpp"

namespace mondoc::app {

struct CompositionRoot {
    CompositionRoot(mondoc::adapters::storage::SqliteConnection conn,
                    mondoc::adapters::ai::LlmConfig llmConfig);

    // Callers must not invoke reconfigureLlm() while an AI fill is in progress —
    // it destroys the pipeline a running fill may still reference. Plan 05-07
    // disables the Settings action during active fills to enforce this.
    void reconfigureLlm(mondoc::adapters::ai::LlmConfig config);

    const mondoc::adapters::ai::LlmConfig& config() const noexcept { return config_; }

    mondoc::adapters::storage::SqliteConnection conn_;
    mondoc::adapters::storage::SqliteTemplateRepository repo_;
    mondoc::adapters::storage::SqliteFillSessionRepository fill_session_repo_;
    mondoc::services::TemplateService service_;
    std::unique_ptr<mondoc::adapters::ai::LlmClient> llm_client_;
    std::unique_ptr<mondoc::adapters::ai::AiFillPipeline> ai_pipeline_;
    std::unique_ptr<mondoc::adapters::ai::AiFieldDetector> ai_field_detector_;
    mondoc::services::FillSessionService fill_session_service_;

private:
    mondoc::adapters::ai::LlmConfig config_;
};

}  // namespace mondoc::app
