#pragma once

#include <filesystem>
#include <memory>

#include <SQLiteCpp/Database.h>

#include "mondoc/expected.hpp"
#include "mondoc/error.hpp"

namespace mondoc::adapters::storage {

class SqliteConnection {
public:
    static std::expected<SqliteConnection, mondoc::Error>
    open(const std::filesystem::path& dbPath);

    SQLite::Database& raw() noexcept { return *db_; }
    const SQLite::Database& raw() const noexcept { return *db_; }

    SqliteConnection(SqliteConnection&&) noexcept = default;
    SqliteConnection& operator=(SqliteConnection&&) noexcept = default;

    SqliteConnection(const SqliteConnection&) = delete;
    SqliteConnection& operator=(const SqliteConnection&) = delete;

private:
    explicit SqliteConnection(std::unique_ptr<SQLite::Database> db) noexcept
        : db_(std::move(db)) {}

    std::unique_ptr<SQLite::Database> db_;
};

}  // namespace mondoc::adapters::storage
