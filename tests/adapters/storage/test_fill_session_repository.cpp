#include <catch2/catch_test_macros.hpp>

#include <SQLiteCpp/Database.h>
#include <SQLiteCpp/Statement.h>

#include <cstdint>
#include <string>
#include <utility>

#include "migrations.hpp"
#include "sqlite_connection.hpp"
#include "sqlite_fill_session_repository.hpp"

using namespace mondoc::adapters::storage;
using mondoc::FieldId;
using mondoc::FillSessionId;
using mondoc::TemplateId;
using mondoc::domain::Fill;
using mondoc::domain::FillSession;
using mondoc::domain::FillStatus;

namespace {

mondoc::expected<SqliteConnection, mondoc::Error> openMigratedDb() {
    auto conn = SqliteConnection::open(":memory:");
    if (!conn) return conn;
    auto migr = runMigrations(*conn);
    if (!migr) return mondoc::unexpected(migr.error());
    auto& db = conn->raw();
    SQLite::Statement t(db,
        "INSERT INTO templates(id, name, source_format, schema_json, blob_path,"
        " blob_hash, created_at, updated_at, version)"
        " VALUES('tpl1','Test','txt','[]','','',0,0,1)");
    t.exec();
    return conn;
}

FillSession makeSession(const std::string& id,
                        FillStatus status = FillStatus::Created,
                        std::int64_t createdAt = 1700000000) {
    FillSession s;
    s.id_ = FillSessionId{id};
    s.template_id_ = TemplateId{"tpl1"};
    s.status_ = status;
    s.created_at_unix_ = createdAt;
    s.updated_at_unix_ = createdAt;
    return s;
}

Fill makeFill(const std::string& fieldId, const std::string& value) {
    Fill f;
    f.field_id_ = FieldId{fieldId};
    f.current_value_ = value;
    return f;
}

}  // namespace

TEST_CASE("Migration v4 applies once and is idempotent",
          "[storage.fill_session]") {
    auto conn = SqliteConnection::open(":memory:");
    REQUIRE(conn.has_value());
    auto first = runMigrations(*conn);
    REQUIRE(first.has_value());
    REQUIRE(*first == 4);

    auto second = runMigrations(*conn);
    REQUIRE(second.has_value());
    REQUIRE(*second == 4);
}

TEST_CASE("SqliteFillSessionRepository: save+findById round-trips a session with fills",
          "[storage.fill_session]") {
    auto conn = openMigratedDb();
    REQUIRE(conn.has_value());
    SqliteFillSessionRepository repo(*conn);

    auto s = makeSession("s1");
    s.fills_.push_back(makeFill("f1", "Acme"));
    s.fills_.push_back(makeFill("f2", "42"));
    REQUIRE(repo.save(s).has_value());

    auto loaded = repo.findById(FillSessionId{"s1"});
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->id_.value() == "s1");
    REQUIRE(loaded->template_id_.value() == "tpl1");
    REQUIRE(loaded->status_ == FillStatus::Created);
    REQUIRE(loaded->fills_.size() == 2);
    REQUIRE(loaded->fills_[0].field_id_.value() == "f1");
    REQUIRE(loaded->fills_[0].current_value_ == "Acme");
    REQUIRE(loaded->fills_[1].field_id_.value() == "f2");
    REQUIRE(loaded->fills_[1].current_value_ == "42");
}

TEST_CASE("SqliteFillSessionRepository: save is upsert — second save replaces fills",
          "[storage.fill_session]") {
    auto conn = openMigratedDb();
    REQUIRE(conn.has_value());
    SqliteFillSessionRepository repo(*conn);

    auto s = makeSession("s1");
    s.fills_.push_back(makeFill("f1", "old1"));
    s.fills_.push_back(makeFill("f2", "old2"));
    REQUIRE(repo.save(s).has_value());

    s.fills_.clear();
    s.fills_.push_back(makeFill("f9", "only"));
    REQUIRE(repo.save(s).has_value());

    auto loaded = repo.findById(FillSessionId{"s1"});
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->fills_.size() == 1);
    REQUIRE(loaded->fills_[0].field_id_.value() == "f9");
    REQUIRE(loaded->fills_[0].current_value_ == "only");
}

TEST_CASE("SqliteFillSessionRepository: listDrafts returns only Created/Reviewing ordered by updated_at DESC",
          "[storage.fill_session]") {
    auto conn = openMigratedDb();
    REQUIRE(conn.has_value());
    SqliteFillSessionRepository repo(*conn);

    REQUIRE(repo.save(makeSession("s1", FillStatus::Created)).has_value());
    REQUIRE(repo.save(makeSession("s2", FillStatus::Reviewing)).has_value());
    REQUIRE(repo.save(makeSession("s3", FillStatus::Exported)).has_value());
    REQUIRE(repo.save(makeSession("s4", FillStatus::Discarded)).has_value());
    REQUIRE(repo.save(makeSession("s5", FillStatus::Failed)).has_value());

    auto& db = conn->raw();
    SQLite::Statement bump(db, "UPDATE fill_sessions SET updated_at = ? WHERE id = ?");
    bump.bind(1, static_cast<int64_t>(100));
    bump.bind(2, std::string{"s1"});
    bump.exec();
    bump.reset();
    bump.clearBindings();
    bump.bind(1, static_cast<int64_t>(200));
    bump.bind(2, std::string{"s2"});
    bump.exec();

    auto drafts = repo.listDrafts();
    REQUIRE(drafts.has_value());
    REQUIRE(drafts->size() == 2);
    REQUIRE((*drafts)[0].id_.value() == "s2");
    REQUIRE((*drafts)[1].id_.value() == "s1");
}

TEST_CASE("SqliteFillSessionRepository: remove deletes session and cascades to fill_values",
          "[storage.fill_session]") {
    auto conn = openMigratedDb();
    REQUIRE(conn.has_value());
    SqliteFillSessionRepository repo(*conn);

    auto s = makeSession("s1");
    s.fills_.push_back(makeFill("f1", "a"));
    s.fills_.push_back(makeFill("f2", "b"));
    s.fills_.push_back(makeFill("f3", "c"));
    REQUIRE(repo.save(s).has_value());

    REQUIRE(repo.remove(FillSessionId{"s1"}).has_value());

    auto& db = conn->raw();
    SQLite::Statement q(db,
        "SELECT COUNT(*) FROM fill_values WHERE session_id = ?");
    q.bind(1, std::string{"s1"});
    REQUIRE(q.executeStep());
    REQUIRE(q.getColumn(0).getInt() == 0);
}

TEST_CASE("SqliteFillSessionRepository: upsertValue first inserts, second updates",
          "[storage.fill_session]") {
    auto conn = openMigratedDb();
    REQUIRE(conn.has_value());
    SqliteFillSessionRepository repo(*conn);

    REQUIRE(repo.save(makeSession("s1")).has_value());

    REQUIRE(repo.upsertValue(FillSessionId{"s1"}, FieldId{"f1"}, "v1").has_value());
    REQUIRE(repo.upsertValue(FillSessionId{"s1"}, FieldId{"f1"}, "v2").has_value());

    auto& db = conn->raw();
    SQLite::Statement count(db,
        "SELECT COUNT(*) FROM fill_values WHERE session_id = ? AND field_id = ?");
    count.bind(1, std::string{"s1"});
    count.bind(2, std::string{"f1"});
    REQUIRE(count.executeStep());
    REQUIRE(count.getColumn(0).getInt() == 1);

    SQLite::Statement value(db,
        "SELECT value FROM fill_values WHERE session_id = ? AND field_id = ?");
    value.bind(1, std::string{"s1"});
    value.bind(2, std::string{"f1"});
    REQUIRE(value.executeStep());
    REQUIRE(value.getColumn(0).getString() == "v2");
}

TEST_CASE("SqliteFillSessionRepository: findById returns notFound for unknown id",
          "[storage.fill_session]") {
    auto conn = openMigratedDb();
    REQUIRE(conn.has_value());
    SqliteFillSessionRepository repo(*conn);

    auto r = repo.findById(FillSessionId{"missing"});
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().kind() == mondoc::Error::Kind::NotFound);
}

TEST_CASE("SqliteFillSessionRepository: all six FillStatus variants round-trip",
          "[storage.fill_session]") {
    auto conn = openMigratedDb();
    REQUIRE(conn.has_value());
    SqliteFillSessionRepository repo(*conn);

    const FillStatus all[] = {
        FillStatus::Created,
        FillStatus::Pipelining,
        FillStatus::Reviewing,
        FillStatus::Exported,
        FillStatus::Failed,
        FillStatus::Discarded,
    };

    int idx = 0;
    for (FillStatus st : all) {
        const std::string id = "s" + std::to_string(idx++);
        REQUIRE(repo.save(makeSession(id, st)).has_value());
        auto loaded = repo.findById(FillSessionId{id});
        REQUIRE(loaded.has_value());
        REQUIRE(loaded->status_ == st);
    }
}
