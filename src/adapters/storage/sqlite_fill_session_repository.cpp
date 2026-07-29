#include "sqlite_fill_session_repository.hpp"

#include "mondoc/util.hpp"

#include <SQLiteCpp/Database.h>
#include <SQLiteCpp/Exception.h>
#include <SQLiteCpp/Statement.h>
#include <SQLiteCpp/Transaction.h>

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace mondoc::adapters::storage {

namespace {

// Single owner of the "draft" status set; every listDrafts query must agree.
constexpr std::string_view kDraftStatusPredicate =
    "s.status IN ('Created','Reviewing')";

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

// Columns: 0 field_id, 1 value, 2 confidence. Queries that group by session
// append session_id as the LAST column.
mondoc::domain::Fill fillFromRow(SQLite::Statement& q) {
    mondoc::domain::Fill f;
    f.field_id_      = mondoc::FieldId{q.getColumn(0).getString()};
    f.current_value_ = q.getColumn(1).getString();
    f.confidence_    = stringToConfidence(q.getColumn(2).getString());
    return f;
}

// Columns: 0 field_id (consumed by the caller for attachment), 1 source_id,
// 2 char_start, 3 char_end, 4 excerpt.
mondoc::domain::SourceRef refFromRow(SQLite::Statement& q) {
    mondoc::domain::SourceRef r;
    r.source_id_    = mondoc::SourceDocId{q.getColumn(1).getString()};
    r.range_.begin_ = q.getColumn(2).getInt64();
    r.range_.end_   = q.getColumn(3).getInt64();
    r.excerpt_      = q.getColumn(4).getString();
    return r;
}

}  // namespace

SqliteFillSessionRepository::SqliteFillSessionRepository(SqliteConnection& conn) noexcept
    : conn_(conn) {}

mondoc::expected<void, mondoc::Error>
SqliteFillSessionRepository::save(const mondoc::domain::FillSession& s) {
    std::lock_guard<std::mutex> lock(conn_.mutex());
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

        SQLite::Statement clearRefs(db,
            "DELETE FROM fill_source_refs WHERE session_id = ?");
        clearRefs.bind(1, s.id_.value());
        clearRefs.exec();

        SQLite::Statement insertVal(db,
            "INSERT INTO fill_values(session_id, field_id, value, updated_at, confidence)"
            " VALUES(?,?,?,?,?)");
        SQLite::Statement insertRef(db,
            "INSERT INTO fill_source_refs"
            "(session_id, field_id, ref_order, source_id, char_start, char_end, excerpt)"
            " VALUES(?,?,?,?,?,?,?)");
        for (const auto& fill : s.fills_) {
            insertVal.reset();
            insertVal.clearBindings();
            insertVal.bind(1, s.id_.value());
            insertVal.bind(2, fill.field_id_.value());
            insertVal.bind(3, fill.current_value_);
            insertVal.bind(4, static_cast<int64_t>(updated));
            insertVal.bind(5, confidenceToString(fill.confidence_));
            insertVal.exec();

            for (std::size_t i = 0; i < fill.source_refs_.size(); ++i) {
                insertRef.reset();
                insertRef.clearBindings();
                insertRef.bind(1, s.id_.value());
                insertRef.bind(2, fill.field_id_.value());
                insertRef.bind(3, static_cast<int64_t>(i));
                insertRef.bind(4, fill.source_refs_[i].source_id_.value());
                insertRef.bind(5, fill.source_refs_[i].range_.begin_);
                insertRef.bind(6, fill.source_refs_[i].range_.end_);
                insertRef.bind(7, fill.source_refs_[i].excerpt_);
                insertRef.exec();
            }
        }

        tx.commit();
        return {};
    } catch (const SQLite::Exception& e) {
        return mondoc::unexpected(mondoc::Error::generic(e.what()));
    }
}

mondoc::expected<mondoc::domain::FillSession, mondoc::Error>
SqliteFillSessionRepository::findById(const mondoc::FillSessionId& id) {
    std::lock_guard<std::mutex> lock(conn_.mutex());
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
            "SELECT field_id, value, confidence FROM fill_values"
            " WHERE session_id = ? ORDER BY field_id ASC");
        qv.bind(1, id.value());
        while (qv.executeStep()) {
            s.fills_.push_back(fillFromRow(qv));
        }

        SQLite::Statement qr(db,
            "SELECT field_id, source_id, char_start, char_end, excerpt"
            " FROM fill_source_refs WHERE session_id = ?"
            " ORDER BY field_id, ref_order");
        qr.bind(1, id.value());
        while (qr.executeStep()) {
            mondoc::FieldId fid{qr.getColumn(0).getString()};
            for (auto& f : s.fills_) {
                if (f.field_id_ == fid) {
                    f.source_refs_.push_back(refFromRow(qr));
                    break;
                }
            }
        }

        return s;
    } catch (const SQLite::Exception& e) {
        return mondoc::unexpected(mondoc::Error::generic(e.what()));
    }
}

mondoc::expected<std::vector<mondoc::domain::FillSession>, mondoc::Error>
SqliteFillSessionRepository::listDrafts() {
    std::lock_guard<std::mutex> lock(conn_.mutex());
    auto& db = conn_.raw();
    try {
        std::vector<mondoc::domain::FillSession> out;
        std::vector<std::string> ids;
        {
            SQLite::Statement q(db,
                "SELECT id, template_id, status, created_at, updated_at"
                " FROM fill_sessions s"
                " WHERE " + std::string{kDraftStatusPredicate} +
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

        std::unordered_map<std::string, std::size_t> indexById;
        for (std::size_t i = 0; i < out.size(); ++i) indexById[ids[i]] = i;

        SQLite::Statement qv(db,
            "SELECT v.field_id, v.value, v.confidence, v.session_id"
            " FROM fill_values v JOIN fill_sessions s ON s.id = v.session_id"
            " WHERE " + std::string{kDraftStatusPredicate} +
            " ORDER BY v.session_id, v.field_id ASC");
        while (qv.executeStep()) {
            auto it = indexById.find(qv.getColumn(3).getString());
            if (it == indexById.end()) continue;
            out[it->second].fills_.push_back(fillFromRow(qv));
        }

        SQLite::Statement qr(db,
            "SELECT r.field_id, r.source_id, r.char_start, r.char_end, r.excerpt, r.session_id"
            " FROM fill_source_refs r JOIN fill_sessions s ON s.id = r.session_id"
            " WHERE " + std::string{kDraftStatusPredicate} +
            " ORDER BY r.session_id, r.field_id, r.ref_order");
        while (qr.executeStep()) {
            auto it = indexById.find(qr.getColumn(5).getString());
            if (it == indexById.end()) continue;
            mondoc::FieldId fid{qr.getColumn(0).getString()};
            for (auto& f : out[it->second].fills_) {
                if (f.field_id_ == fid) {
                    f.source_refs_.push_back(refFromRow(qr));
                    break;
                }
            }
        }

        return out;
    } catch (const SQLite::Exception& e) {
        return mondoc::unexpected(mondoc::Error::generic(e.what()));
    }
}

mondoc::expected<void, mondoc::Error>
SqliteFillSessionRepository::remove(const mondoc::FillSessionId& id) {
    std::lock_guard<std::mutex> lock(conn_.mutex());
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
    std::lock_guard<std::mutex> lock(conn_.mutex());
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
SqliteFillSessionRepository::setValueManual(const mondoc::FillSessionId& sessionId,
                                             const mondoc::FieldId& fieldId,
                                             const std::string& value) {
    std::lock_guard<std::mutex> lock(conn_.mutex());
    auto& db = conn_.raw();
    try {
        const auto now = static_cast<int64_t>(unixNowSeconds());

        SQLite::Transaction tx(db);

        // Single atomic statement: value and confidence='manual' are written
        // together, so a concurrent AI write can never observe (or land in)
        // a window where the value is the user's but the confidence is not
        // yet Manual. A direct user edit always wins, unconditionally.
        SQLite::Statement upsert(db,
            "INSERT INTO fill_values(session_id, field_id, value, updated_at, confidence)"
            " VALUES(?,?,?,?,'manual')"
            " ON CONFLICT(session_id, field_id) DO UPDATE SET"
            "  value=excluded.value,"
            "  confidence='manual',"
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

mondoc::expected<bool, mondoc::Error>
SqliteFillSessionRepository::upsertValueIfNotManual(const mondoc::FillSessionId& sessionId,
                                                     const mondoc::FieldId& fieldId,
                                                     const std::string& value) {
    std::lock_guard<std::mutex> lock(conn_.mutex());
    auto& db = conn_.raw();
    try {
        const auto now = static_cast<int64_t>(unixNowSeconds());

        SQLite::Transaction tx(db);

        // Single atomic statement: the WHERE clause on the DO UPDATE branch
        // reads the row's *current* confidence/value (the ON CONFLICT target),
        // so a user's Manual, non-empty edit protects itself even if it lands
        // in the microsecond window between an AI read and this write.
        SQLite::Statement upsert(db,
            "INSERT INTO fill_values(session_id, field_id, value, updated_at)"
            " VALUES(?,?,?,?)"
            " ON CONFLICT(session_id, field_id) DO UPDATE SET"
            "  value=excluded.value,"
            "  updated_at=excluded.updated_at"
            " WHERE NOT (confidence = 'manual' AND value <> '')");
        upsert.bind(1, sessionId.value());
        upsert.bind(2, fieldId.value());
        upsert.bind(3, value);
        upsert.bind(4, now);
        upsert.exec();

        const bool written = db.getChanges() > 0;
        if (written) {
            SQLite::Statement bump(db,
                "UPDATE fill_sessions SET updated_at = ? WHERE id = ?");
            bump.bind(1, now);
            bump.bind(2, sessionId.value());
            bump.exec();
        }

        tx.commit();
        return written;
    } catch (const SQLite::Exception& e) {
        return mondoc::unexpected(mondoc::Error::generic(e.what()));
    }
}

mondoc::expected<void, mondoc::Error>
SqliteFillSessionRepository::upsertConfidence(const mondoc::FillSessionId& sessionId,
                                               const mondoc::FieldId& fieldId,
                                               mondoc::domain::Confidence confidence) {
    std::lock_guard<std::mutex> lock(conn_.mutex());
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
    std::lock_guard<std::mutex> lock(conn_.mutex());
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
