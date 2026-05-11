#pragma once

#include <memory>
#include <utility>

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

    mondoc::adapters::storage::SqliteConnection conn_;
    mondoc::adapters::storage::SqliteTemplateRepository repo_;
    mondoc::adapters::storage::SqliteFillSessionRepository fill_session_repo_;
    mondoc::services::TemplateService service_;
    std::unique_ptr<mondoc::adapters::ai::LlmClient> llm_client_;
    std::unique_ptr<mondoc::adapters::ai::AiFillPipeline> ai_pipeline_;
    mondoc::services::FillSessionService fill_session_service_;
};

}  // namespace mondoc::app
