#pragma once

#include <string>
#include <vector>

#include "domain/i_fill_session_repository.hpp"
#include "sqlite_connection.hpp"

namespace mondoc::adapters::storage {

class SqliteFillSessionRepository : public mondoc::domain::IFillSessionRepository {
public:
    explicit SqliteFillSessionRepository(SqliteConnection& conn) noexcept;

    mondoc::expected<void, mondoc::Error>
        save(const mondoc::domain::FillSession& session) override;

    mondoc::expected<mondoc::domain::FillSession, mondoc::Error>
        findById(const mondoc::FillSessionId& id) override;

    mondoc::expected<std::vector<mondoc::domain::FillSession>, mondoc::Error>
        listDrafts() override;

    mondoc::expected<void, mondoc::Error>
        remove(const mondoc::FillSessionId& id) override;

    mondoc::expected<void, mondoc::Error>
        upsertValue(const mondoc::FillSessionId& sessionId,
                    const mondoc::FieldId& fieldId,
                    const std::string& value) override;

    mondoc::expected<void, mondoc::Error>
        upsertConfidence(const mondoc::FillSessionId& sessionId,
                         const mondoc::FieldId& fieldId,
                         mondoc::domain::Confidence confidence) override;

    mondoc::expected<void, mondoc::Error>
        replaceSourceRefs(const mondoc::FillSessionId& sessionId,
                          const mondoc::FieldId& fieldId,
                          const std::vector<mondoc::domain::SourceRef>& refs) override;

private:
    SqliteConnection& conn_;
};

}  // namespace mondoc::adapters::storage
