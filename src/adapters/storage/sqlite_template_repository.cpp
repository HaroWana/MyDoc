#include "sqlite_template_repository.hpp"

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

// path.u8string() returns UTF-8 unconditionally; path.string() uses the OS
// native ANSI codepage on Windows and mangles non-ASCII characters.
std::string pathToUtf8(const std::filesystem::path& p) {
    auto u8 = p.u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

std::filesystem::path utf8ToPath(const std::string& s) {
    std::u8string u8(reinterpret_cast<const char8_t*>(s.data()), s.size());
    return std::filesystem::path(u8);
}

std::int64_t unixNowSeconds() noexcept {
    using namespace std::chrono;
    return duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
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

}  // namespace

SqliteTemplateRepository::SqliteTemplateRepository(SqliteConnection& conn) noexcept
    : conn_(conn) {}

mondoc::expected<void, mondoc::Error>
SqliteTemplateRepository::save(const mondoc::domain::Template& t) {
    auto& db = conn_.raw();
    try {
        SQLite::Transaction tx(db);

        const auto now = static_cast<int64_t>(unixNowSeconds());

        SQLite::Statement upsert(db,
            "INSERT OR REPLACE INTO templates"
            "(id, name, source_format, schema_json, blob_path, blob_hash,"
            " created_at, updated_at, version)"
            " VALUES(?,?,?,?,?,?,?,?,?)");
        upsert.bind(1, t.id_.value());
        upsert.bind(2, t.name_);
        upsert.bind(3, t.source_format_);
        upsert.bind(4, std::string{"[]"});
        upsert.bind(5, pathToUtf8(t.source_path_));
        upsert.bind(6, std::string{""});
        upsert.bind(7, now);
        upsert.bind(8, now);
        upsert.bind(9, 1);
        upsert.exec();

        SQLite::Statement clearFields(db,
            "DELETE FROM template_fields WHERE template_id = ?");
        clearFields.bind(1, t.id_.value());
        clearFields.exec();

        SQLite::Statement insertField(db,
            "INSERT INTO template_fields(id, template_id, name, type, order_idx)"
            " VALUES(?,?,?,?,?)");
        for (std::size_t i = 0; i < t.fields_.size(); ++i) {
            const auto& f = t.fields_[i];
            insertField.reset();
            insertField.clearBindings();
            insertField.bind(1, f.id_.value());
            insertField.bind(2, t.id_.value());
            insertField.bind(3, f.name_);
            insertField.bind(4, fieldTypeToString(f.type_));
            insertField.bind(5, static_cast<int64_t>(i));
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
            "SELECT id, name, type FROM template_fields"
            " WHERE template_id = ? ORDER BY order_idx ASC");
        qf.bind(1, id.value());
        while (qf.executeStep()) {
            mondoc::domain::Field f;
            f.id_   = mondoc::FieldId{qf.getColumn(0).getString()};
            f.name_ = qf.getColumn(1).getString();
            f.type_ = stringToFieldType(qf.getColumn(2).getString());
            t.fields_.push_back(std::move(f));
        }

        return t;
    } catch (const SQLite::Exception& e) {
        return mondoc::unexpected(mondoc::Error::generic(e.what()));
    }
}

mondoc::expected<std::vector<mondoc::domain::Template>, mondoc::Error>
SqliteTemplateRepository::listAll() {
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
            "SELECT id, name, type FROM template_fields"
            " WHERE template_id = ? ORDER BY order_idx ASC");
        for (std::size_t i = 0; i < out.size(); ++i) {
            qf.reset();
            qf.clearBindings();
            qf.bind(1, ids[i]);
            while (qf.executeStep()) {
                mondoc::domain::Field f;
                f.id_   = mondoc::FieldId{qf.getColumn(0).getString()};
                f.name_ = qf.getColumn(1).getString();
                f.type_ = stringToFieldType(qf.getColumn(2).getString());
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
