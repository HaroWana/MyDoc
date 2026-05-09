#pragma once

#include <utility>

#include "fill_session_service.hpp"
#include "sqlite_connection.hpp"
#include "sqlite_fill_session_repository.hpp"
#include "sqlite_template_repository.hpp"
#include "template_service.hpp"

namespace mondoc::app {

struct CompositionRoot {
    explicit CompositionRoot(mondoc::adapters::storage::SqliteConnection conn)
        : conn_(std::move(conn)),
          repo_(conn_),
          fill_session_repo_(conn_),
          service_(repo_),
          fill_session_service_(fill_session_repo_, repo_) {}

    mondoc::adapters::storage::SqliteConnection conn_;
    mondoc::adapters::storage::SqliteTemplateRepository repo_;
    mondoc::adapters::storage::SqliteFillSessionRepository fill_session_repo_;
    mondoc::services::TemplateService service_;
    mondoc::services::FillSessionService fill_session_service_;
};

}  // namespace mondoc::app
