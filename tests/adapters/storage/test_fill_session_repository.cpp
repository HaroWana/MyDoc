#include <catch2/catch_test_macros.hpp>

#include <SQLiteCpp/Database.h>
#include <SQLiteCpp/Statement.h>

#include <cstdint>
#include <string>
#include <utility>

#include "domain/confidence.hpp"
#include "domain/source_ref.hpp"
#include "migrations.hpp"
#include "sqlite_connection.hpp"
#include "sqlite_fill_session_repository.hpp"

using namespace mondoc::adapters::storage;
using mondoc::FieldId;
using mondoc::FillSessionId;
using mondoc::SourceDocId;
using mondoc::TemplateId;
using mondoc::domain::Confidence;
using mondoc::domain::Fill;
using mondoc::domain::FillSession;
using mondoc::domain::FillStatus;
using mondoc::domain::SourceRef;
using mondoc::domain::TextRange;

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

TEST_CASE("Migrations apply once and are idempotent",
          "[storage.fill_session]") {
    auto conn = SqliteConnection::open(":memory:");
    REQUIRE(conn.has_value());
    auto first = runMigrations(*conn);
    REQUIRE(first.has_value());
    REQUIRE(*first == 6);

    auto second = runMigrations(*conn);
    REQUIRE(second.has_value());
    REQUIRE(*second == 6);
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

TEST_CASE("SqliteFillSessionRepository: setValueManual updates value AND confidence "
          "in one call over an AI-filled row",
          "[storage.fill_session][dsa-4]") {
    auto conn = openMigratedDb();
    REQUIRE(conn.has_value());
    SqliteFillSessionRepository repo(*conn);
    REQUIRE(repo.save(makeSession("s1")).has_value());
    REQUIRE(repo.upsertValue(FillSessionId{"s1"}, FieldId{"f1"}, "ai guess").has_value());
    REQUIRE(repo.upsertConfidence(FillSessionId{"s1"}, FieldId{"f1"}, Confidence::High).has_value());

    REQUIRE(repo.setValueManual(FillSessionId{"s1"}, FieldId{"f1"}, "USER TYPED").has_value());

    auto session = repo.findById(FillSessionId{"s1"});
    REQUIRE(session.has_value());
    REQUIRE(session->fills_.size() == 1);
    REQUIRE(session->fills_[0].current_value_ == "USER TYPED");
    REQUIRE(session->fills_[0].confidence_ == Confidence::Manual);
}

TEST_CASE("SqliteFillSessionRepository: setValueManual on a fresh row gets Manual",
          "[storage.fill_session][dsa-4]") {
    auto conn = openMigratedDb();
    REQUIRE(conn.has_value());
    SqliteFillSessionRepository repo(*conn);
    REQUIRE(repo.save(makeSession("s1")).has_value());

    REQUIRE(repo.setValueManual(FillSessionId{"s1"}, FieldId{"f1"}, "typed").has_value());

    auto session = repo.findById(FillSessionId{"s1"});
    REQUIRE(session.has_value());
    REQUIRE(session->fills_.size() == 1);
    REQUIRE(session->fills_[0].current_value_ == "typed");
    REQUIRE(session->fills_[0].confidence_ == Confidence::Manual);
}

TEST_CASE("SqliteFillSessionRepository: upsertValueIfNotManual writes when no row exists yet",
          "[storage.fill_session][dsa-4]") {
    auto conn = openMigratedDb();
    REQUIRE(conn.has_value());
    SqliteFillSessionRepository repo(*conn);
    REQUIRE(repo.save(makeSession("s1")).has_value());

    auto written = repo.upsertValueIfNotManual(FillSessionId{"s1"}, FieldId{"f1"}, "ai value");
    REQUIRE(written.has_value());
    REQUIRE(*written == true);

    auto session = repo.findById(FillSessionId{"s1"});
    REQUIRE(session.has_value());
    REQUIRE(session->fills_.size() == 1);
    REQUIRE(session->fills_[0].current_value_ == "ai value");
}

TEST_CASE("SqliteFillSessionRepository: upsertValueIfNotManual overwrites a non-manual row",
          "[storage.fill_session][dsa-4]") {
    auto conn = openMigratedDb();
    REQUIRE(conn.has_value());
    SqliteFillSessionRepository repo(*conn);
    REQUIRE(repo.save(makeSession("s1")).has_value());
    REQUIRE(repo.upsertValue(FillSessionId{"s1"}, FieldId{"f1"}, "first ai guess").has_value());
    REQUIRE(repo.upsertConfidence(FillSessionId{"s1"}, FieldId{"f1"}, Confidence::High).has_value());

    auto written = repo.upsertValueIfNotManual(FillSessionId{"s1"}, FieldId{"f1"}, "second ai guess");
    REQUIRE(written.has_value());
    REQUIRE(*written == true);

    auto session = repo.findById(FillSessionId{"s1"});
    REQUIRE(session.has_value());
    REQUIRE(session->fills_[0].current_value_ == "second ai guess");
}

TEST_CASE("SqliteFillSessionRepository: upsertValueIfNotManual skips a Manual, non-empty row atomically",
          "[storage.fill_session][dsa-4]") {
    auto conn = openMigratedDb();
    REQUIRE(conn.has_value());
    SqliteFillSessionRepository repo(*conn);
    REQUIRE(repo.save(makeSession("s1")).has_value());
    REQUIRE(repo.upsertValue(FillSessionId{"s1"}, FieldId{"f1"}, "USER TYPED").has_value());
    REQUIRE(repo.upsertConfidence(FillSessionId{"s1"}, FieldId{"f1"}, Confidence::Manual).has_value());

    auto written = repo.upsertValueIfNotManual(FillSessionId{"s1"}, FieldId{"f1"}, "ai overwrite");
    REQUIRE(written.has_value());
    REQUIRE(*written == false);

    auto session = repo.findById(FillSessionId{"s1"});
    REQUIRE(session.has_value());
    REQUIRE(session->fills_[0].current_value_ == "USER TYPED");
    REQUIRE(session->fills_[0].confidence_ == Confidence::Manual);
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

TEST_CASE("save + findById round-trips Confidence::High",
          "[adapters.storage][fill_session_repo][phase03]") {
    auto conn = openMigratedDb();
    REQUIRE(conn.has_value());
    SqliteFillSessionRepository repo(*conn);

    auto s = makeSession("s1");
    Fill f;
    f.field_id_      = FieldId{"name"};
    f.current_value_ = "John Doe";
    f.confidence_    = Confidence::High;
    s.fills_.push_back(std::move(f));
    REQUIRE(repo.save(s).has_value());

    auto loaded = repo.findById(FillSessionId{"s1"});
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->fills_.size() == 1);
    REQUIRE(loaded->fills_[0].confidence_ == Confidence::High);
    REQUIRE(loaded->fills_[0].current_value_ == "John Doe");
    REQUIRE(loaded->fills_[0].source_refs_.empty());
}

TEST_CASE("save + findById round-trips source refs in order",
          "[adapters.storage][fill_session_repo][phase03]") {
    auto conn = openMigratedDb();
    REQUIRE(conn.has_value());
    SqliteFillSessionRepository repo(*conn);

    auto s = makeSession("s1");
    Fill f;
    f.field_id_      = FieldId{"name"};
    f.current_value_ = "John Doe";
    f.confidence_    = Confidence::Medium;
    f.source_refs_.push_back(SourceRef{SourceDocId{"src-a"}, TextRange{12, 20}, "John Doe"});
    f.source_refs_.push_back(SourceRef{SourceDocId{"src-b"}, TextRange{100, 110}, "Sept 2020"});
    s.fills_.push_back(std::move(f));
    REQUIRE(repo.save(s).has_value());

    auto loaded = repo.findById(FillSessionId{"s1"});
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->fills_.size() == 1);
    REQUIRE(loaded->fills_[0].source_refs_.size() == 2);
    REQUIRE(loaded->fills_[0].source_refs_[0].source_id_.value() == "src-a");
    REQUIRE(loaded->fills_[0].source_refs_[0].range_.begin_ == 12);
    REQUIRE(loaded->fills_[0].source_refs_[0].range_.end_ == 20);
    REQUIRE(loaded->fills_[0].source_refs_[0].excerpt_ == "John Doe");
    REQUIRE(loaded->fills_[0].source_refs_[1].source_id_.value() == "src-b");
    REQUIRE(loaded->fills_[0].source_refs_[1].range_.begin_ == 100);
}

TEST_CASE("upsertConfidence after upsertValue updates row in place",
          "[adapters.storage][fill_session_repo][phase03]") {
    auto conn = openMigratedDb();
    REQUIRE(conn.has_value());
    SqliteFillSessionRepository repo(*conn);

    REQUIRE(repo.save(makeSession("s1")).has_value());
    REQUIRE(repo.upsertValue(FillSessionId{"s1"}, FieldId{"f1"}, "manual value").has_value());
    REQUIRE(repo.upsertConfidence(FillSessionId{"s1"}, FieldId{"f1"}, Confidence::Medium).has_value());

    auto loaded = repo.findById(FillSessionId{"s1"});
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->fills_.size() == 1);
    REQUIRE(loaded->fills_[0].current_value_ == "manual value");
    REQUIRE(loaded->fills_[0].confidence_ == Confidence::Medium);
}

TEST_CASE("replaceSourceRefs deletes prior refs before insert",
          "[adapters.storage][fill_session_repo][phase03]") {
    auto conn = openMigratedDb();
    REQUIRE(conn.has_value());
    SqliteFillSessionRepository repo(*conn);

    auto s = makeSession("s1");
    Fill f;
    f.field_id_      = FieldId{"name"};
    f.current_value_ = "John";
    f.source_refs_.push_back(SourceRef{SourceDocId{"src-a"}, TextRange{0, 4}, "John"});
    f.source_refs_.push_back(SourceRef{SourceDocId{"src-b"}, TextRange{10, 14}, "Doe!"});
    f.source_refs_.push_back(SourceRef{SourceDocId{"src-c"}, TextRange{20, 24}, "Jane"});
    s.fills_.push_back(std::move(f));
    REQUIRE(repo.save(s).has_value());

    std::vector<SourceRef> oneRef;
    oneRef.push_back(SourceRef{SourceDocId{"src-z"}, TextRange{99, 105}, "final"});
    REQUIRE(repo.replaceSourceRefs(FillSessionId{"s1"}, FieldId{"name"}, oneRef).has_value());

    auto loaded = repo.findById(FillSessionId{"s1"});
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->fills_.size() == 1);
    REQUIRE(loaded->fills_[0].source_refs_.size() == 1);
    REQUIRE(loaded->fills_[0].source_refs_[0].source_id_.value() == "src-z");
    REQUIRE(loaded->fills_[0].source_refs_[0].excerpt_ == "final");
}

TEST_CASE("listDrafts hydrates confidence and source refs for every fill",
          "[adapters.storage][fill_session_repo][phase03]") {
    auto conn = openMigratedDb();
    REQUIRE(conn.has_value());
    SqliteFillSessionRepository repo(*conn);

    for (const char* sid : {"s1", "s2"}) {
        auto s = makeSession(sid);
        for (const char* fid : {"f1", "f2"}) {
            Fill f;
            f.field_id_      = FieldId{fid};
            f.current_value_ = std::string{fid} + "-val";
            f.confidence_    = Confidence::Medium;
            f.source_refs_.push_back(SourceRef{SourceDocId{"src"}, TextRange{0, 3}, "abc"});
            s.fills_.push_back(std::move(f));
        }
        REQUIRE(repo.save(s).has_value());
    }

    auto drafts = repo.listDrafts();
    REQUIRE(drafts.has_value());
    REQUIRE(drafts->size() == 2);
    for (const auto& sess : *drafts) {
        REQUIRE(sess.fills_.size() == 2);
        for (const auto& fill : sess.fills_) {
            REQUIRE(fill.confidence_ == Confidence::Medium);
            REQUIRE(fill.source_refs_.size() == 1);
            REQUIRE(fill.source_refs_[0].excerpt_ == "abc");
        }
    }
}

TEST_CASE("Confidence::Manual is the default for a never-AI-touched fill",
          "[adapters.storage][fill_session_repo][phase03]") {
    auto conn = openMigratedDb();
    REQUIRE(conn.has_value());
    SqliteFillSessionRepository repo(*conn);

    REQUIRE(repo.save(makeSession("s1")).has_value());
    REQUIRE(repo.upsertValue(FillSessionId{"s1"}, FieldId{"f1"}, "typed").has_value());

    auto loaded = repo.findById(FillSessionId{"s1"});
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->fills_.size() == 1);
    REQUIRE(loaded->fills_[0].confidence_ == Confidence::Manual);
}

TEST_CASE("remove cascades to fill_source_refs",
          "[adapters.storage][fill_session_repo][phase03]") {
    auto conn = openMigratedDb();
    REQUIRE(conn.has_value());
    SqliteFillSessionRepository repo(*conn);

    auto s = makeSession("s1");
    Fill f;
    f.field_id_      = FieldId{"name"};
    f.current_value_ = "John";
    f.source_refs_.push_back(SourceRef{SourceDocId{"src-a"}, TextRange{0, 4}, "John"});
    s.fills_.push_back(std::move(f));
    REQUIRE(repo.save(s).has_value());

    REQUIRE(repo.remove(FillSessionId{"s1"}).has_value());

    auto& db = conn->raw();
    SQLite::Statement q(db,
        "SELECT COUNT(*) FROM fill_source_refs WHERE session_id = ?");
    q.bind(1, std::string{"s1"});
    REQUIRE(q.executeStep());
    REQUIRE(q.getColumn(0).getInt() == 0);
}
