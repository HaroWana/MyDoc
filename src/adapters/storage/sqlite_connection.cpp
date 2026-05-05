#include "sqlite_connection.hpp"

#include <SQLiteCpp/Exception.h>

#include <string>
#include <utility>

namespace mondoc::adapters::storage {

namespace {

// path.u8string() returns UTF-8 unconditionally; path.string() uses the OS
// native ANSI codepage on Windows and mangles non-ASCII characters
// (RESEARCH.md Risk R3 / PITFALLS C4).
std::string pathToUtf8(const std::filesystem::path& p) {
    auto u8 = p.u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

}  // namespace

std::expected<SqliteConnection, mondoc::Error>
SqliteConnection::open(const std::filesystem::path& dbPath) {
    try {
        auto db = std::make_unique<SQLite::Database>(
            pathToUtf8(dbPath),
            SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);

        try {
            db->exec("PRAGMA journal_mode = WAL;");
        } catch (const SQLite::Exception&) {
            // :memory: databases keep the default journal mode.
        }
        db->exec("PRAGMA foreign_keys = ON;");
        db->exec("PRAGMA synchronous = NORMAL;");

        return SqliteConnection(std::move(db));
    } catch (const SQLite::Exception& e) {
        return std::unexpected(mondoc::Error::storageOpen(e.what()));
    }
}

}  // namespace mondoc::adapters::storage
