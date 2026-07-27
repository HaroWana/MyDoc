#include <catch2/catch_test_macros.hpp>

#include <SQLiteCpp/Database.h>
#include <SQLiteCpp/Statement.h>

#include "sqlite_connection.hpp"
#include "migrations.hpp"

#include <filesystem>
#include <string>

using namespace mondoc::adapters::storage;

namespace {

int readUserVersion(SQLite::Database& db) {
    SQLite::Statement q(db, "PRAGMA user_version;");
    REQUIRE(q.executeStep());
    return q.getColumn(0).getInt();
}

int countSchemaMigrations(SQLite::Database& db) {
    SQLite::Statement q(db, "SELECT COUNT(*) FROM schema_migrations;");
    REQUIRE(q.executeStep());
    return q.getColumn(0).getInt();
}

}  // namespace

TEST_CASE("SqliteConnection: foreign_keys ON after open", "[storage]") {
    auto conn = SqliteConnection::open(":memory:");
    REQUIRE(conn.has_value());
    SQLite::Statement q(conn->raw(), "PRAGMA foreign_keys;");
    REQUIRE(q.executeStep());
    REQUIRE(q.getColumn(0).getInt() == 1);
}

TEST_CASE("SqliteConnection: synchronous = NORMAL (1) after open", "[storage]") {
    auto conn = SqliteConnection::open(":memory:");
    REQUIRE(conn.has_value());
    SQLite::Statement q(conn->raw(), "PRAGMA synchronous;");
    REQUIRE(q.executeStep());
    REQUIRE(q.getColumn(0).getInt() == 1);
}

TEST_CASE("SqliteConnection: journal_mode is wal or memory", "[storage]") {
    auto conn = SqliteConnection::open(":memory:");
    REQUIRE(conn.has_value());
    SQLite::Statement q(conn->raw(), "PRAGMA journal_mode;");
    REQUIRE(q.executeStep());
    auto mode = q.getColumn(0).getString();
    REQUIRE((mode == "wal" || mode == "memory"));
}

TEST_CASE("runMigrations: applies all migrations and bumps user_version", "[storage]") {
    auto conn = SqliteConnection::open(":memory:");
    REQUIRE(conn.has_value());

    auto result = runMigrations(*conn);
    REQUIRE(result.has_value());
    int expectedVersion = static_cast<int>(registeredMigrations().size());
    REQUIRE(*result == expectedVersion);
    REQUIRE(readUserVersion(conn->raw()) == expectedVersion);
}

TEST_CASE("runMigrations: schema_migrations row count equals migration count", "[storage]") {
    auto conn = SqliteConnection::open(":memory:");
    REQUIRE(conn.has_value());
    REQUIRE(runMigrations(*conn).has_value());

    REQUIRE(countSchemaMigrations(conn->raw())
            == static_cast<int>(registeredMigrations().size()));
}

TEST_CASE("runMigrations: re-running is a no-op", "[storage]") {
    auto conn = SqliteConnection::open(":memory:");
    REQUIRE(conn.has_value());

    auto first = runMigrations(*conn);
    REQUIRE(first.has_value());

    auto second = runMigrations(*conn);
    REQUIRE(second.has_value());
    REQUIRE(*second == *first);
    REQUIRE(countSchemaMigrations(conn->raw())
            == static_cast<int>(registeredMigrations().size()));
}

TEST_CASE("runMigrations: templates table exists after v1", "[storage]") {
    auto conn = SqliteConnection::open(":memory:");
    REQUIRE(conn.has_value());
    REQUIRE(runMigrations(*conn).has_value());

    SQLite::Statement q(conn->raw(),
        "SELECT name FROM sqlite_master WHERE type='table' AND name='templates';");
    REQUIRE(q.executeStep());
    REQUIRE(q.getColumn(0).getString() == "templates");
}

TEST_CASE("[TST-12] SqliteConnection::open: nonexistent parent directory returns an error",
          "[storage][tst-12]") {
    auto conn = SqliteConnection::open(
        std::filesystem::path{"/nonexistent/dir/db.sqlite"});
    REQUIRE_FALSE(conn.has_value());
}

TEST_CASE("SqliteConnection: opens a unicode path", "[storage]") {
    auto dir = std::filesystem::temp_directory_path();
    auto path = dir / std::filesystem::path(std::u8string{u8"é-mondoc-test.db"});
    std::error_code ec;
    std::filesystem::remove(path, ec);

    {
        auto conn = SqliteConnection::open(path);
        REQUIRE(conn.has_value());
        REQUIRE(runMigrations(*conn).has_value());
    }

    std::filesystem::remove(path, ec);
}
