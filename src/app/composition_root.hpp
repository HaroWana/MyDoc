#pragma once

#include <utility>

#include "sqlite_connection.hpp"
#include "sqlite_template_repository.hpp"
#include "template_service.hpp"

namespace mondoc::app {

struct CompositionRoot {
    explicit CompositionRoot(mondoc::adapters::storage::SqliteConnection conn)
        : conn_(std::move(conn)),
          repo_(conn_),
          service_(repo_) {}

    mondoc::adapters::storage::SqliteConnection conn_;
    mondoc::adapters::storage::SqliteTemplateRepository repo_;
    mondoc::services::TemplateService service_;
};

}  // namespace mondoc::app
