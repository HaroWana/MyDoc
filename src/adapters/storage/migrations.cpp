#include "migrations.hpp"

#include "mondoc/util.hpp"

#include <SQLiteCpp/Database.h>
#include <SQLiteCpp/Exception.h>
#include <SQLiteCpp/Statement.h>
#include <SQLiteCpp/Transaction.h>

#include <array>
#include <string>

namespace mondoc::adapters::storage {

namespace {

constexpr std::string_view kV1Sql = R"SQL(
CREATE TABLE IF NOT EXISTS schema_migrations (
    version    INTEGER PRIMARY KEY,
    applied_at INTEGER NOT NULL
);
CREATE TABLE IF NOT EXISTS templates (
    id            TEXT PRIMARY KEY,
    name          TEXT NOT NULL,
    source_format TEXT NOT NULL,
    schema_json   TEXT NOT NULL,
    blob_path     TEXT NOT NULL,
    blob_hash     TEXT NOT NULL,
    created_at    INTEGER NOT NULL,
    updated_at    INTEGER NOT NULL,
    version       INTEGER NOT NULL DEFAULT 1
);
)SQL";

constexpr std::string_view kV2Sql = R"SQL(
CREATE TABLE IF NOT EXISTS template_fields (
    id          TEXT NOT NULL,
    template_id TEXT NOT NULL REFERENCES templates(id) ON DELETE CASCADE,
    name        TEXT NOT NULL,
    type        TEXT NOT NULL,
    order_idx   INTEGER NOT NULL,
    PRIMARY KEY (id)
);
)SQL";

constexpr std::string_view kV3Sql = R"SQL(
CREATE TABLE IF NOT EXISTS fill_sessions (
    id            TEXT PRIMARY KEY,
    template_id   TEXT NOT NULL REFERENCES templates(id) ON DELETE CASCADE,
    status        TEXT NOT NULL,
    created_at    INTEGER NOT NULL,
    updated_at    INTEGER NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_fill_sessions_status_updated
    ON fill_sessions(status, updated_at DESC);
CREATE TABLE IF NOT EXISTS fill_values (
    session_id    TEXT NOT NULL REFERENCES fill_sessions(id) ON DELETE CASCADE,
    field_id      TEXT NOT NULL,
    value         TEXT NOT NULL,
    updated_at    INTEGER NOT NULL,
    PRIMARY KEY (session_id, field_id)
);
)SQL";

constexpr std::string_view kV4Sql = R"SQL(
ALTER TABLE fill_values ADD COLUMN confidence TEXT NOT NULL DEFAULT 'manual';
CREATE TABLE IF NOT EXISTS fill_source_refs (
    session_id  TEXT NOT NULL,
    field_id    TEXT NOT NULL,
    ref_order   INTEGER NOT NULL,
    source_id   TEXT NOT NULL,
    char_start  INTEGER NOT NULL,
    char_end    INTEGER NOT NULL,
    excerpt     TEXT NOT NULL,
    PRIMARY KEY (session_id, field_id, ref_order),
    FOREIGN KEY (session_id) REFERENCES fill_sessions(id) ON DELETE CASCADE
);
CREATE INDEX IF NOT EXISTS idx_fill_source_refs_session_field
    ON fill_source_refs(session_id, field_id);
)SQL";

constexpr std::string_view kV5Sql = R"SQL(
ALTER TABLE template_fields ADD COLUMN origin TEXT NOT NULL DEFAULT 'unknown';
)SQL";

constexpr std::string_view kV6Sql = R"SQL(
ALTER TABLE template_fields ADD COLUMN location_json TEXT;
)SQL";

constexpr std::array<Migration, 6> kMigrations{
    Migration{1, kV1Sql},
    Migration{2, kV2Sql},
    Migration{3, kV3Sql},
    Migration{4, kV4Sql},
    Migration{5, kV5Sql},
    Migration{6, kV6Sql},
};

}  // namespace

std::span<const Migration> registeredMigrations() noexcept {
    return std::span<const Migration>(kMigrations.data(), kMigrations.size());
}

mondoc::expected<int, mondoc::Error> runMigrations(SqliteConnection& conn) {
    auto& db = conn.raw();

    int currentVersion = 0;
    try {
        SQLite::Statement q(db, "PRAGMA user_version;");
        if (q.executeStep()) {
            currentVersion = q.getColumn(0).getInt();
        }
    } catch (const SQLite::Exception& e) {
        return mondoc::unexpected(mondoc::Error::migration(
            std::string{"read user_version: "} + e.what()));
    }

    for (const auto& m : kMigrations) {
        if (m.version <= currentVersion) {
            continue;
        }
        try {
            SQLite::Transaction tx(db);
            db.exec(std::string{m.sql});
            SQLite::Statement insert(
                db,
                "INSERT OR IGNORE INTO schema_migrations(version, applied_at) VALUES(?, ?)");
            insert.bind(1, m.version);
            insert.bind(2, static_cast<int64_t>(unixNowSeconds()));
            insert.exec();
            db.exec("PRAGMA user_version = " + std::to_string(m.version) + ";");
            tx.commit();
            currentVersion = m.version;
        } catch (const SQLite::Exception& e) {
            return mondoc::unexpected(mondoc::Error::migration(
                "v" + std::to_string(m.version) + ": " + e.what()));
        }
    }

    return currentVersion;
}

}  // namespace mondoc::adapters::storage
