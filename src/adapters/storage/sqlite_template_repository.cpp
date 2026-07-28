#include "sqlite_template_repository.hpp"

#include "mondoc/util.hpp"

#include <SQLiteCpp/Database.h>
#include <SQLiteCpp/Exception.h>
#include <SQLiteCpp/Statement.h>
#include <SQLiteCpp/Transaction.h>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace mondoc::adapters::storage {

namespace {

std::filesystem::path utf8ToPath(const std::string& s) {
    std::u8string u8(reinterpret_cast<const char8_t*>(s.data()), s.size());
    return std::filesystem::path(u8);
}

std::string fieldTypeToString(mondoc::domain::FieldType t) {
    using mondoc::domain::FieldType;
    switch (t) {
        case FieldType::Text:      return "Text";
        case FieldType::Paragraph: return "Paragraph";
        case FieldType::Number:    return "Number";
        case FieldType::Date:      return "Date";
        case FieldType::Checkbox:  return "Checkbox";
        case FieldType::Dropdown:  return "Dropdown";
    }
    return "Text";
}

mondoc::domain::FieldType stringToFieldType(const std::string& s) {
    using mondoc::domain::FieldType;
    if (s == "Paragraph") return FieldType::Paragraph;
    if (s == "Number")    return FieldType::Number;
    if (s == "Date")      return FieldType::Date;
    if (s == "Checkbox")  return FieldType::Checkbox;
    if (s == "Dropdown")  return FieldType::Dropdown;
    return FieldType::Text;
}

std::string fieldOriginToString(mondoc::domain::FieldOrigin o) {
    using mondoc::domain::FieldOrigin;
    switch (o) {
        case FieldOrigin::FormControl: return "form_control";
        case FieldOrigin::Placeholder: return "placeholder";
        case FieldOrigin::Ai:          return "ai";
        case FieldOrigin::Unknown:     return "unknown";
    }
    return "unknown";
}

mondoc::domain::FieldOrigin stringToFieldOrigin(const std::string& s) {
    using mondoc::domain::FieldOrigin;
    if (s == "form_control") return FieldOrigin::FormControl;
    if (s == "placeholder")  return FieldOrigin::Placeholder;
    if (s == "ai")           return FieldOrigin::Ai;
    return FieldOrigin::Unknown;
}

std::string locationToJson(const std::optional<mondoc::domain::FieldLocation>& loc) {
    if (!loc.has_value()) return "";
    nlohmann::json j;
    if (loc->pdf.has_value()) {
        j["type"]       = "pdf";
        j["page_index"] = loc->pdf->page_index;
        j["x"]          = loc->pdf->x;
        j["y"]          = loc->pdf->y;
        j["w"]          = loc->pdf->w;
        j["h"]          = loc->pdf->h;
    } else if (loc->text.has_value()) {
        j["type"]            = "text";
        j["paragraph_index"] = loc->text->paragraph_index;
        j["char_offset"]     = loc->text->char_offset;
    } else {
        return "";
    }
    return j.dump();
}

std::optional<mondoc::domain::FieldLocation> locationFromJson(const std::string& s) {
    if (s.empty()) return std::nullopt;
    try {
        auto j = nlohmann::json::parse(s);
        if (!j.contains("type")) return std::nullopt;
        const std::string type = j["type"].get<std::string>();
        if (type == "pdf") {
            mondoc::domain::PdfLocation pl{};
            pl.page_index = j.value("page_index", 0);
            pl.x = j.value("x", 0.0);
            pl.y = j.value("y", 0.0);
            pl.w = j.value("w", 0.0);
            pl.h = j.value("h", 0.0);
            return mondoc::domain::FieldLocation{pl, std::nullopt};
        }
        if (type == "text") {
            mondoc::domain::TextLocation tl{};
            tl.paragraph_index = j.value("paragraph_index", 0);
            tl.char_offset = j.value("char_offset", 0);
            return mondoc::domain::FieldLocation{std::nullopt, tl};
        }
    } catch (...) {}
    return std::nullopt;
}

}  // namespace

SqliteTemplateRepository::SqliteTemplateRepository(SqliteConnection& conn) noexcept
    : conn_(conn) {}

mondoc::expected<void, mondoc::Error>
SqliteTemplateRepository::save(const mondoc::domain::Template& t) {
    std::lock_guard<std::mutex> lock(conn_.mutex());
    auto& db = conn_.raw();
    try {
        SQLite::Transaction tx(db);

        const auto now = static_cast<int64_t>(unixNowSeconds());

        SQLite::Statement upsert(db,
            "INSERT INTO templates"
            "(id, name, source_format, blob_path, created_at, updated_at)"
            " VALUES(?,?,?,?,?,?)"
            " ON CONFLICT(id) DO UPDATE SET"
            "  name=excluded.name,"
            "  source_format=excluded.source_format,"
            "  blob_path=excluded.blob_path,"
            "  updated_at=excluded.updated_at");
        upsert.bind(1, t.id_.value());
        upsert.bind(2, t.name_);
        upsert.bind(3, t.source_format_);
        upsert.bind(4, pathToUtf8(t.source_path_));
        upsert.bind(5, now);
        upsert.bind(6, now);
        upsert.exec();

        SQLite::Statement clearFields(db,
            "DELETE FROM template_fields WHERE template_id = ?");
        clearFields.bind(1, t.id_.value());
        clearFields.exec();

        SQLite::Statement insertField(db,
            "INSERT INTO template_fields(id, template_id, name, type, origin, order_idx, location_json)"
            " VALUES(?,?,?,?,?,?,?)");
        for (std::size_t i = 0; i < t.fields_.size(); ++i) {
            const auto& f = t.fields_[i];
            insertField.reset();
            insertField.clearBindings();
            insertField.bind(1, f.id_.value());
            insertField.bind(2, t.id_.value());
            insertField.bind(3, f.name_);
            insertField.bind(4, fieldTypeToString(f.type_));
            insertField.bind(5, fieldOriginToString(f.origin_));
            insertField.bind(6, static_cast<int64_t>(i));
            const std::string locJson = locationToJson(f.location_);
            if (locJson.empty())
                insertField.bind(7);
            else
                insertField.bind(7, locJson);
            insertField.exec();
        }

        tx.commit();
        return {};
    } catch (const SQLite::Exception& e) {
        return mondoc::unexpected(mondoc::Error::generic(e.what()));
    }
}

mondoc::expected<mondoc::domain::Template, mondoc::Error>
SqliteTemplateRepository::findById(const mondoc::TemplateId& id) {
    std::lock_guard<std::mutex> lock(conn_.mutex());
    auto& db = conn_.raw();
    try {
        mondoc::domain::Template t;

        {
            SQLite::Statement q(db,
                "SELECT id, name, source_format, blob_path"
                " FROM templates WHERE id = ?");
            q.bind(1, id.value());
            if (!q.executeStep()) {
                return mondoc::unexpected(mondoc::Error::notFound(
                    "template not found: " + id.value()));
            }
            t.id_            = mondoc::TemplateId{q.getColumn(0).getString()};
            t.name_          = q.getColumn(1).getString();
            t.source_format_ = q.getColumn(2).getString();
            t.source_path_   = utf8ToPath(q.getColumn(3).getString());
        }

        SQLite::Statement qf(db,
            "SELECT id, name, type, origin, location_json FROM template_fields"
            " WHERE template_id = ? ORDER BY order_idx ASC");
        qf.bind(1, id.value());
        while (qf.executeStep()) {
            mondoc::domain::Field f;
            f.id_     = mondoc::FieldId{qf.getColumn(0).getString()};
            f.name_   = qf.getColumn(1).getString();
            f.type_   = stringToFieldType(qf.getColumn(2).getString());
            f.origin_ = stringToFieldOrigin(qf.getColumn(3).getString());
            const std::string locStr = qf.getColumn(4).isNull()
                ? std::string{}
                : qf.getColumn(4).getString();
            f.location_ = locationFromJson(locStr);
            t.fields_.push_back(std::move(f));
        }

        return t;
    } catch (const SQLite::Exception& e) {
        return mondoc::unexpected(mondoc::Error::generic(e.what()));
    }
}

mondoc::expected<std::vector<mondoc::domain::Template>, mondoc::Error>
SqliteTemplateRepository::listAll() {
    std::lock_guard<std::mutex> lock(conn_.mutex());
    auto& db = conn_.raw();
    try {
        std::vector<mondoc::domain::Template> out;

        std::vector<std::string> ids;
        {
            SQLite::Statement q(db,
                "SELECT id, name, source_format, blob_path"
                " FROM templates ORDER BY name ASC");
            while (q.executeStep()) {
                mondoc::domain::Template t;
                t.id_            = mondoc::TemplateId{q.getColumn(0).getString()};
                t.name_          = q.getColumn(1).getString();
                t.source_format_ = q.getColumn(2).getString();
                t.source_path_   = utf8ToPath(q.getColumn(3).getString());
                ids.push_back(t.id_.value());
                out.push_back(std::move(t));
            }
        }

        SQLite::Statement qf(db,
            "SELECT id, name, type, origin, location_json FROM template_fields"
            " WHERE template_id = ? ORDER BY order_idx ASC");
        for (std::size_t i = 0; i < out.size(); ++i) {
            qf.reset();
            qf.clearBindings();
            qf.bind(1, ids[i]);
            while (qf.executeStep()) {
                mondoc::domain::Field f;
                f.id_     = mondoc::FieldId{qf.getColumn(0).getString()};
                f.name_   = qf.getColumn(1).getString();
                f.type_   = stringToFieldType(qf.getColumn(2).getString());
                f.origin_ = stringToFieldOrigin(qf.getColumn(3).getString());
                const std::string locStr = qf.getColumn(4).isNull()
                    ? std::string{}
                    : qf.getColumn(4).getString();
                f.location_ = locationFromJson(locStr);
                out[i].fields_.push_back(std::move(f));
            }
        }

        return out;
    } catch (const SQLite::Exception& e) {
        return mondoc::unexpected(mondoc::Error::generic(e.what()));
    }
}

mondoc::expected<void, mondoc::Error>
SqliteTemplateRepository::remove(const mondoc::TemplateId& id) {
    std::lock_guard<std::mutex> lock(conn_.mutex());
    auto& db = conn_.raw();
    try {
        SQLite::Statement del(db, "DELETE FROM templates WHERE id = ?");
        del.bind(1, id.value());
        del.exec();
        if (db.getChanges() == 0) {
            return mondoc::unexpected(mondoc::Error::notFound(
                "template not found: " + id.value()));
        }
        return {};
    } catch (const SQLite::Exception& e) {
        return mondoc::unexpected(mondoc::Error::generic(e.what()));
    }
}

}  // namespace mondoc::adapters::storage
