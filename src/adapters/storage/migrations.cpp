#include "migrations.hpp"

#include <SQLiteCpp/Database.h>
#include <SQLiteCpp/Exception.h>
#include <SQLiteCpp/Statement.h>
#include <SQLiteCpp/Transaction.h>

#include <array>
#include <chrono>
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

constexpr std::array<Migration, 1> kMigrations{
    Migration{1, kV1Sql},
};

std::int64_t unixNowSeconds() noexcept {
    using namespace std::chrono;
    return duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
}

}  // namespace

std::span<const Migration> registeredMigrations() noexcept {
    return std::span<const Migration>(kMigrations.data(), kMigrations.size());
}

std::expected<int, mondoc::Error> runMigrations(SqliteConnection& conn) {
    auto& db = conn.raw();

    int currentVersion = 0;
    try {
        SQLite::Statement q(db, "PRAGMA user_version;");
        if (q.executeStep()) {
            currentVersion = q.getColumn(0).getInt();
        }
    } catch (const SQLite::Exception& e) {
        return std::unexpected(mondoc::Error::migration(
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
                "INSERT INTO schema_migrations(version, applied_at) VALUES(?, ?)");
            insert.bind(1, m.version);
            insert.bind(2, static_cast<long long>(unixNowSeconds()));
            insert.exec();
            db.exec("PRAGMA user_version = " + std::to_string(m.version) + ";");
            tx.commit();
            currentVersion = m.version;
        } catch (const SQLite::Exception& e) {
            return std::unexpected(mondoc::Error::migration(
                "v" + std::to_string(m.version) + ": " + e.what()));
        }
    }

    return currentVersion;
}

}  // namespace mondoc::adapters::storage
