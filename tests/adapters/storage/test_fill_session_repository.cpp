#include <catch2/catch_test_macros.hpp>

#include "migrations.hpp"
#include "sqlite_connection.hpp"
#include "sqlite_fill_session_repository.hpp"

using namespace mondoc::adapters::storage;

TEST_CASE("Migration v3 applies once and is idempotent",
          "[storage.fill_session]") {
    auto conn = SqliteConnection::open(":memory:");
    REQUIRE(conn.has_value());
    auto first = runMigrations(*conn);
    REQUIRE(first.has_value());
    REQUIRE(*first == 3);

    auto second = runMigrations(*conn);
    REQUIRE(second.has_value());
    REQUIRE(*second == 3);
}

TEST_CASE("SqliteFillSessionRepository: round-trip save+findById (Plan 05)",
          "[storage.fill_session][!shouldfail]") {
    REQUIRE(false);
}
