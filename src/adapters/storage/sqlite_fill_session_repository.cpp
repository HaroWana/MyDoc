#include "sqlite_fill_session_repository.hpp"

#include <SQLiteCpp/Database.h>
#include <SQLiteCpp/Exception.h>
#include <SQLiteCpp/Statement.h>
#include <SQLiteCpp/Transaction.h>

#include <chrono>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace mondoc::adapters::storage {

namespace {

std::int64_t unixNowSeconds() noexcept {
    using namespace std::chrono;
    return duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
}

std::string fillStatusToString(mondoc::domain::FillStatus s) {
    using mondoc::domain::FillStatus;
    switch (s) {
        case FillStatus::Created:    return "Created";
        case FillStatus::Pipelining: return "Pipelining";
        case FillStatus::Reviewing:  return "Reviewing";
        case FillStatus::Exported:   return "Exported";
        case FillStatus::Failed:     return "Failed";
        case FillStatus::Discarded:  return "Discarded";
    }
    return "Created";
}

mondoc::domain::FillStatus stringToFillStatus(const std::string& s) {
    using mondoc::domain::FillStatus;
    if (s == "Pipelining") return FillStatus::Pipelining;
    if (s == "Reviewing")  return FillStatus::Reviewing;
    if (s == "Exported")   return FillStatus::Exported;
    if (s == "Failed")     return FillStatus::Failed;
    if (s == "Discarded")  return FillStatus::Discarded;
    return FillStatus::Created;
}

std::string confidenceToString(mondoc::domain::Confidence c) {
    using mondoc::domain::Confidence;
    switch (c) {
        case Confidence::High:   return "high";
        case Confidence::Medium: return "medium";
        case Confidence::Low:    return "low";
        case Confidence::Manual: return "manual";
    }
    return "manual";
}

mondoc::domain::Confidence stringToConfidence(const std::string& s) {
    using mondoc::domain::Confidence;
    if (s == "high")   return Confidence::High;
    if (s == "medium") return Confidence::Medium;
    if (s == "low")    return Confidence::Low;
    return Confidence::Manual;
}

}  // namespace

SqliteFillSessionRepository::SqliteFillSessionRepository(SqliteConnection& conn) noexcept
    : conn_(conn) {}

mondoc::expected<void, mondoc::Error>
SqliteFillSessionRepository::save(const mondoc::domain::FillSession& s) {
    auto& db = conn_.raw();
    try {
        SQLite::Transaction tx(db);

        const auto now = static_cast<int64_t>(unixNowSeconds());
        const auto created = s.created_at_unix_ != 0 ? s.created_at_unix_ : now;
        const auto updated = now;

        SQLite::Statement upsert(db,
            "INSERT INTO fill_sessions"
            "(id, template_id, status, created_at, updated_at)"
            " VALUES(?,?,?,?,?)"
            " ON CONFLICT(id) DO UPDATE SET"
            "  template_id=excluded.template_id,"
            "  status=excluded.status,"
            "  updated_at=excluded.updated_at");
        upsert.bind(1, s.id_.value());
        upsert.bind(2, s.template_id_.value());
        upsert.bind(3, fillStatusToString(s.status_));
        upsert.bind(4, static_cast<int64_t>(created));
        upsert.bind(5, static_cast<int64_t>(updated));
        upsert.exec();

        SQLite::Statement clearVals(db,
            "DELETE FROM fill_values WHERE session_id = ?");
        clearVals.bind(1, s.id_.value());
        clearVals.exec();

        SQLite::Statement insertVal(db,
            "INSERT INTO fill_values(session_id, field_id, value, updated_at)"
            " VALUES(?,?,?,?)");
        for (const auto& fill : s.fills_) {
            insertVal.reset();
            insertVal.clearBindings();
            insertVal.bind(1, s.id_.value());
            insertVal.bind(2, fill.field_id_.value());
            insertVal.bind(3, fill.current_value_);
            insertVal.bind(4, static_cast<int64_t>(updated));
            insertVal.exec();
        }

        tx.commit();
        return {};
    } catch (const SQLite::Exception& e) {
        return mondoc::unexpected(mondoc::Error::generic(e.what()));
    }
}

mondoc::expected<mondoc::domain::FillSession, mondoc::Error>
SqliteFillSessionRepository::findById(const mondoc::FillSessionId& id) {
    auto& db = conn_.raw();
    try {
        mondoc::domain::FillSession s;
        {
            SQLite::Statement q(db,
                "SELECT id, template_id, status, created_at, updated_at"
                " FROM fill_sessions WHERE id = ?");
            q.bind(1, id.value());
            if (!q.executeStep()) {
                return mondoc::unexpected(mondoc::Error::notFound(
                    "fill session not found: " + id.value()));
            }
            s.id_              = mondoc::FillSessionId{q.getColumn(0).getString()};
            s.template_id_     = mondoc::TemplateId{q.getColumn(1).getString()};
            s.status_          = stringToFillStatus(q.getColumn(2).getString());
            s.created_at_unix_ = q.getColumn(3).getInt64();
            s.updated_at_unix_ = q.getColumn(4).getInt64();
        }

        SQLite::Statement qv(db,
            "SELECT field_id, value FROM fill_values"
            " WHERE session_id = ? ORDER BY field_id ASC");
        qv.bind(1, id.value());
        while (qv.executeStep()) {
            mondoc::domain::Fill f;
            f.field_id_      = mondoc::FieldId{qv.getColumn(0).getString()};
            f.current_value_ = qv.getColumn(1).getString();
            s.fills_.push_back(std::move(f));
        }

        return s;
    } catch (const SQLite::Exception& e) {
        return mondoc::unexpected(mondoc::Error::generic(e.what()));
    }
}

mondoc::expected<std::vector<mondoc::domain::FillSession>, mondoc::Error>
SqliteFillSessionRepository::listDrafts() {
    auto& db = conn_.raw();
    try {
        std::vector<mondoc::domain::FillSession> out;
        std::vector<std::string> ids;
        {
            SQLite::Statement q(db,
                "SELECT id, template_id, status, created_at, updated_at"
                " FROM fill_sessions"
                " WHERE status IN ('Created','Reviewing')"
                " ORDER BY updated_at DESC");
            while (q.executeStep()) {
                mondoc::domain::FillSession s;
                s.id_              = mondoc::FillSessionId{q.getColumn(0).getString()};
                s.template_id_     = mondoc::TemplateId{q.getColumn(1).getString()};
                s.status_          = stringToFillStatus(q.getColumn(2).getString());
                s.created_at_unix_ = q.getColumn(3).getInt64();
                s.updated_at_unix_ = q.getColumn(4).getInt64();
                ids.push_back(s.id_.value());
                out.push_back(std::move(s));
            }
        }

        SQLite::Statement qv(db,
            "SELECT field_id, value FROM fill_values"
            " WHERE session_id = ? ORDER BY field_id ASC");
        for (std::size_t i = 0; i < out.size(); ++i) {
            qv.reset();
            qv.clearBindings();
            qv.bind(1, ids[i]);
            while (qv.executeStep()) {
                mondoc::domain::Fill f;
                f.field_id_      = mondoc::FieldId{qv.getColumn(0).getString()};
                f.current_value_ = qv.getColumn(1).getString();
                out[i].fills_.push_back(std::move(f));
            }
        }

        return out;
    } catch (const SQLite::Exception& e) {
        return mondoc::unexpected(mondoc::Error::generic(e.what()));
    }
}

mondoc::expected<void, mondoc::Error>
SqliteFillSessionRepository::remove(const mondoc::FillSessionId& id) {
    auto& db = conn_.raw();
    try {
        SQLite::Statement del(db, "DELETE FROM fill_sessions WHERE id = ?");
        del.bind(1, id.value());
        del.exec();
        if (db.getChanges() == 0) {
            return mondoc::unexpected(mondoc::Error::notFound(
                "fill session not found: " + id.value()));
        }
        return {};
    } catch (const SQLite::Exception& e) {
        return mondoc::unexpected(mondoc::Error::generic(e.what()));
    }
}

mondoc::expected<void, mondoc::Error>
SqliteFillSessionRepository::upsertValue(const mondoc::FillSessionId& sessionId,
                                          const mondoc::FieldId& fieldId,
                                          const std::string& value) {
    auto& db = conn_.raw();
    try {
        const auto now = static_cast<int64_t>(unixNowSeconds());

        SQLite::Transaction tx(db);

        SQLite::Statement upsert(db,
            "INSERT INTO fill_values(session_id, field_id, value, updated_at)"
            " VALUES(?,?,?,?)"
            " ON CONFLICT(session_id, field_id) DO UPDATE SET"
            "  value=excluded.value,"
            "  updated_at=excluded.updated_at");
        upsert.bind(1, sessionId.value());
        upsert.bind(2, fieldId.value());
        upsert.bind(3, value);
        upsert.bind(4, now);
        upsert.exec();

        SQLite::Statement bump(db,
            "UPDATE fill_sessions SET updated_at = ? WHERE id = ?");
        bump.bind(1, now);
        bump.bind(2, sessionId.value());
        bump.exec();

        tx.commit();
        return {};
    } catch (const SQLite::Exception& e) {
        return mondoc::unexpected(mondoc::Error::generic(e.what()));
    }
}

mondoc::expected<void, mondoc::Error>
SqliteFillSessionRepository::upsertConfidence(const mondoc::FillSessionId& sessionId,
                                               const mondoc::FieldId& fieldId,
                                               mondoc::domain::Confidence confidence) {
    auto& db = conn_.raw();
    try {
        const auto now = static_cast<int64_t>(unixNowSeconds());

        SQLite::Transaction tx(db);

        SQLite::Statement upsert(db,
            "INSERT INTO fill_values(session_id, field_id, value, updated_at, confidence)"
            " VALUES(?,?,'',?,?)"
            " ON CONFLICT(session_id, field_id) DO UPDATE SET"
            "  confidence=excluded.confidence,"
            "  updated_at=excluded.updated_at");
        upsert.bind(1, sessionId.value());
        upsert.bind(2, fieldId.value());
        upsert.bind(3, now);
        upsert.bind(4, confidenceToString(confidence));
        upsert.exec();

        SQLite::Statement bump(db,
            "UPDATE fill_sessions SET updated_at = ? WHERE id = ?");
        bump.bind(1, now);
        bump.bind(2, sessionId.value());
        bump.exec();

        tx.commit();
        return {};
    } catch (const SQLite::Exception& e) {
        return mondoc::unexpected(mondoc::Error::generic(e.what()));
    }
}

mondoc::expected<void, mondoc::Error>
SqliteFillSessionRepository::replaceSourceRefs(const mondoc::FillSessionId& sessionId,
                                                const mondoc::FieldId& fieldId,
                                                const std::vector<mondoc::domain::SourceRef>& refs) {
    auto& db = conn_.raw();
    try {
        SQLite::Transaction tx(db);

        SQLite::Statement del(db,
            "DELETE FROM fill_source_refs"
            " WHERE session_id = ? AND field_id = ?");
        del.bind(1, sessionId.value());
        del.bind(2, fieldId.value());
        del.exec();

        SQLite::Statement ins(db,
            "INSERT INTO fill_source_refs"
            "(session_id, field_id, ref_order, source_id, char_start, char_end, excerpt)"
            " VALUES(?,?,?,?,?,?,?)");
        for (std::size_t i = 0; i < refs.size(); ++i) {
            ins.reset();
            ins.clearBindings();
            ins.bind(1, sessionId.value());
            ins.bind(2, fieldId.value());
            ins.bind(3, static_cast<int64_t>(i));
            ins.bind(4, refs[i].source_id_.value());
            ins.bind(5, refs[i].range_.begin_);
            ins.bind(6, refs[i].range_.end_);
            ins.bind(7, refs[i].excerpt_);
            ins.exec();
        }

        tx.commit();
        return {};
    } catch (const SQLite::Exception& e) {
        return mondoc::unexpected(mondoc::Error::generic(e.what()));
    }
}

}  // namespace mondoc::adapters::storage
