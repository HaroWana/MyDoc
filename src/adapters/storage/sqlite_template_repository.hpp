#pragma once

#include <vector>

#include "domain/i_template_repository.hpp"
#include "sqlite_connection.hpp"

namespace mondoc::adapters::storage {

class SqliteTemplateRepository : public mondoc::domain::ITemplateRepository {
public:
    explicit SqliteTemplateRepository(SqliteConnection& conn) noexcept;

    mondoc::expected<void, mondoc::Error>
        save(const mondoc::domain::Template& t) override;

    mondoc::expected<mondoc::domain::Template, mondoc::Error>
        findById(const mondoc::TemplateId& id) override;

    mondoc::expected<std::vector<mondoc::domain::Template>, mondoc::Error>
        listAll() override;

    mondoc::expected<void, mondoc::Error>
        remove(const mondoc::TemplateId& id) override;

private:
    SqliteConnection& conn_;
};

}  // namespace mondoc::adapters::storage
