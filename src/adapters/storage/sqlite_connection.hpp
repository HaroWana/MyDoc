#pragma once

#include <filesystem>
#include <memory>
#include <mutex>

#include <SQLiteCpp/Database.h>

#include "mondoc/expected.hpp"
#include "mondoc/error.hpp"

namespace mondoc::adapters::storage {

class SqliteConnection {
public:
    static mondoc::expected<SqliteConnection, mondoc::Error>
    open(const std::filesystem::path& dbPath);

    SQLite::Database& raw() noexcept { return *db_; }
    const SQLite::Database& raw() const noexcept { return *db_; }

    // Serializes SQLiteCpp Statement/Transaction object lifecycles across the
    // repositories sharing this connection (UI thread + AI worker threads).
    std::mutex& mutex() noexcept { return *mutex_; }

    SqliteConnection(SqliteConnection&&) noexcept = default;
    SqliteConnection& operator=(SqliteConnection&&) noexcept = default;

    SqliteConnection(const SqliteConnection&) = delete;
    SqliteConnection& operator=(const SqliteConnection&) = delete;

private:
    explicit SqliteConnection(std::unique_ptr<SQLite::Database> db) noexcept
        : db_(std::move(db)) {}

    std::unique_ptr<SQLite::Database> db_;
    std::unique_ptr<std::mutex> mutex_ = std::make_unique<std::mutex>();
};

}  // namespace mondoc::adapters::storage
