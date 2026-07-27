#include "sqlite_connection.hpp"

#include "mondoc/util.hpp"

#include <SQLiteCpp/Exception.h>

#include <string>
#include <utility>

namespace mondoc::adapters::storage {

mondoc::expected<SqliteConnection, mondoc::Error>
SqliteConnection::open(const std::filesystem::path& dbPath) {
    try {
        auto db = std::make_unique<SQLite::Database>(
            pathToUtf8(dbPath),
            SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE | SQLite::OPEN_FULLMUTEX);

        try {
            db->exec("PRAGMA journal_mode = WAL;");
        } catch (const SQLite::Exception&) {
            // :memory: databases keep the default journal mode; any other
            // path propagates as the open-error below.
            if (pathToUtf8(dbPath) != ":memory:") {
                throw;
            }
        }
        db->exec("PRAGMA foreign_keys = ON;");
        db->exec("PRAGMA synchronous = NORMAL;");

        return SqliteConnection(std::move(db));
    } catch (const SQLite::Exception& e) {
        return mondoc::unexpected(mondoc::Error::storageOpen(e.what()));
    }
}

}  // namespace mondoc::adapters::storage
