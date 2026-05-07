#include "sqlite_fill_session_repository.hpp"

namespace mondoc::adapters::storage {

SqliteFillSessionRepository::SqliteFillSessionRepository(SqliteConnection& conn) noexcept
    : conn_(conn) {}

mondoc::expected<void, mondoc::Error>
SqliteFillSessionRepository::save(const mondoc::domain::FillSession&) {
    return mondoc::unexpected(mondoc::Error::generic("not implemented"));
}

mondoc::expected<mondoc::domain::FillSession, mondoc::Error>
SqliteFillSessionRepository::findById(const mondoc::FillSessionId&) {
    return mondoc::unexpected(mondoc::Error::generic("not implemented"));
}

mondoc::expected<std::vector<mondoc::domain::FillSession>, mondoc::Error>
SqliteFillSessionRepository::listDrafts() {
    return mondoc::unexpected(mondoc::Error::generic("not implemented"));
}

mondoc::expected<void, mondoc::Error>
SqliteFillSessionRepository::remove(const mondoc::FillSessionId&) {
    return mondoc::unexpected(mondoc::Error::generic("not implemented"));
}

mondoc::expected<void, mondoc::Error>
SqliteFillSessionRepository::upsertValue(const mondoc::FillSessionId&,
                                          const mondoc::FieldId&,
                                          const std::string&) {
    return mondoc::unexpected(mondoc::Error::generic("not implemented"));
}

}  // namespace mondoc::adapters::storage
