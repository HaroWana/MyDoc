#pragma once

#include <span>
#include <string_view>

#include "mondoc/expected.hpp"
#include "mondoc/error.hpp"
#include "sqlite_connection.hpp"

namespace mondoc::adapters::storage {

struct Migration {
    int version;
    std::string_view sql;
};

std::span<const Migration> registeredMigrations() noexcept;

// Applies all migrations whose version is greater than the database's current
// PRAGMA user_version. On success returns the new (highest applied) version.
// On failure returns Error::migration(...) with the offending version embedded.
std::expected<int, mondoc::Error> runMigrations(SqliteConnection& conn);

}  // namespace mondoc::adapters::storage
