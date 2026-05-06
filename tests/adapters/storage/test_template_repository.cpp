#include <catch2/catch_test_macros.hpp>

#include <SQLiteCpp/Database.h>
#include <SQLiteCpp/Statement.h>

#include "migrations.hpp"
#include "sqlite_connection.hpp"
#include "sqlite_template_repository.hpp"

#include <filesystem>
#include <string>

using namespace mondoc::adapters::storage;

namespace {

mondoc::domain::Template makeTemplate(const std::string& id,
                                      const std::string& name,
                                      const std::string& format = "docx") {
    mondoc::domain::Template t;
    t.id_ = mondoc::TemplateId{id};
    t.name_ = name;
    t.source_format_ = format;
    t.source_path_ = std::filesystem::path{"/tmp/test.docx"};
    return t;
}

mondoc::domain::Field makeField(
    const std::string& id,
    const std::string& name,
    mondoc::domain::FieldType type = mondoc::domain::FieldType::Text) {
    mondoc::domain::Field f;
    f.id_   = mondoc::FieldId{id};
    f.name_ = name;
    f.type_ = type;
    return f;
}

}  // namespace

TEST_CASE("SqliteTemplateRepository: save and findById round-trips template metadata",
          "[storage.template_repo]") {
    auto conn = SqliteConnection::open(":memory:");
    REQUIRE(conn.has_value());
    REQUIRE(runMigrations(*conn).has_value());
    SqliteTemplateRepository repo(*conn);

    auto saved = repo.save(makeTemplate("t1", "Invoice"));
    REQUIRE(saved.has_value());

    auto loaded = repo.findById(mondoc::TemplateId{"t1"});
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->id_.value() == "t1");
    REQUIRE(loaded->name_ == "Invoice");
    REQUIRE(loaded->source_format_ == "docx");
}

TEST_CASE("SqliteTemplateRepository: save persists fields with correct types",
          "[storage.template_repo]") {
    auto conn = SqliteConnection::open(":memory:");
    REQUIRE(conn.has_value());
    REQUIRE(runMigrations(*conn).has_value());
    SqliteTemplateRepository repo(*conn);

    auto t = makeTemplate("t1", "Invoice");
    t.fields_.push_back(makeField("f1", "customer_name"));
    t.fields_.push_back(makeField("f2", "due_date", mondoc::domain::FieldType::Date));
    REQUIRE(repo.save(t).has_value());

    auto loaded = repo.findById(mondoc::TemplateId{"t1"});
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->fields_.size() == 2);
    REQUIRE(loaded->fields_[0].name_ == "customer_name");
    REQUIRE(loaded->fields_[0].type_ == mondoc::domain::FieldType::Text);
    REQUIRE(loaded->fields_[1].name_ == "due_date");
    REQUIRE(loaded->fields_[1].type_ == mondoc::domain::FieldType::Date);
}

TEST_CASE("SqliteTemplateRepository: findById returns not-found error for unknown id",
          "[storage.template_repo]") {
    auto conn = SqliteConnection::open(":memory:");
    REQUIRE(conn.has_value());
    REQUIRE(runMigrations(*conn).has_value());
    SqliteTemplateRepository repo(*conn);

    auto result = repo.findById(mondoc::TemplateId{"does-not-exist"});
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().kind() == mondoc::Error::Kind::NotFound);
}

TEST_CASE("SqliteTemplateRepository: listAll returns all saved templates ordered by name",
          "[storage.template_repo]") {
    auto conn = SqliteConnection::open(":memory:");
    REQUIRE(conn.has_value());
    REQUIRE(runMigrations(*conn).has_value());
    SqliteTemplateRepository repo(*conn);

    REQUIRE(repo.save(makeTemplate("z", "Zebra")).has_value());
    REQUIRE(repo.save(makeTemplate("a", "Apple")).has_value());

    auto list = repo.listAll();
    REQUIRE(list.has_value());
    REQUIRE(list->size() == 2);
    REQUIRE((*list)[0].name_ == "Apple");
    REQUIRE((*list)[1].name_ == "Zebra");
}

TEST_CASE("SqliteTemplateRepository: save is an upsert — second save updates the template",
          "[storage.template_repo]") {
    auto conn = SqliteConnection::open(":memory:");
    REQUIRE(conn.has_value());
    REQUIRE(runMigrations(*conn).has_value());
    SqliteTemplateRepository repo(*conn);

    auto t = makeTemplate("t1", "Invoice");
    REQUIRE(repo.save(t).has_value());

    t.name_ = "InvoiceV2";
    REQUIRE(repo.save(t).has_value());

    auto loaded = repo.findById(mondoc::TemplateId{"t1"});
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->name_ == "InvoiceV2");

    auto list = repo.listAll();
    REQUIRE(list.has_value());
    REQUIRE(list->size() == 1);
}

TEST_CASE("SqliteTemplateRepository: save upsert replaces fields",
          "[storage.template_repo]") {
    auto conn = SqliteConnection::open(":memory:");
    REQUIRE(conn.has_value());
    REQUIRE(runMigrations(*conn).has_value());
    SqliteTemplateRepository repo(*conn);

    auto t = makeTemplate("t1", "Invoice");
    t.fields_.push_back(makeField("f1", "a"));
    t.fields_.push_back(makeField("f2", "b"));
    t.fields_.push_back(makeField("f3", "c"));
    REQUIRE(repo.save(t).has_value());

    t.fields_.clear();
    t.fields_.push_back(makeField("f9", "only"));
    REQUIRE(repo.save(t).has_value());

    auto loaded = repo.findById(mondoc::TemplateId{"t1"});
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->fields_.size() == 1);
    REQUIRE(loaded->fields_[0].name_ == "only");
}

TEST_CASE("SqliteTemplateRepository: remove deletes the template",
          "[storage.template_repo]") {
    auto conn = SqliteConnection::open(":memory:");
    REQUIRE(conn.has_value());
    REQUIRE(runMigrations(*conn).has_value());
    SqliteTemplateRepository repo(*conn);

    REQUIRE(repo.save(makeTemplate("t1", "Invoice")).has_value());
    REQUIRE(repo.remove(mondoc::TemplateId{"t1"}).has_value());

    auto list = repo.listAll();
    REQUIRE(list.has_value());
    REQUIRE(list->empty());
}

TEST_CASE("SqliteTemplateRepository: remove cascades to template_fields",
          "[storage.template_repo]") {
    auto conn = SqliteConnection::open(":memory:");
    REQUIRE(conn.has_value());
    REQUIRE(runMigrations(*conn).has_value());
    SqliteTemplateRepository repo(*conn);

    auto t = makeTemplate("t1", "Invoice");
    t.fields_.push_back(makeField("f1", "a"));
    t.fields_.push_back(makeField("f2", "b"));
    REQUIRE(repo.save(t).has_value());

    REQUIRE(repo.remove(mondoc::TemplateId{"t1"}).has_value());

    SQLite::Statement q(conn->raw(),
        "SELECT COUNT(*) FROM template_fields WHERE template_id = ?");
    q.bind(1, std::string{"t1"});
    REQUIRE(q.executeStep());
    REQUIRE(q.getColumn(0).getInt() == 0);
}

TEST_CASE("SqliteTemplateRepository: remove returns not-found error for unknown id",
          "[storage.template_repo]") {
    auto conn = SqliteConnection::open(":memory:");
    REQUIRE(conn.has_value());
    REQUIRE(runMigrations(*conn).has_value());
    SqliteTemplateRepository repo(*conn);

    auto result = repo.remove(mondoc::TemplateId{"does-not-exist"});
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().kind() == mondoc::Error::Kind::NotFound);
}

TEST_CASE("SqliteTemplateRepository: field order preserved (order_idx)",
          "[storage.template_repo]") {
    auto conn = SqliteConnection::open(":memory:");
    REQUIRE(conn.has_value());
    REQUIRE(runMigrations(*conn).has_value());
    SqliteTemplateRepository repo(*conn);

    auto t = makeTemplate("t1", "Invoice");
    t.fields_.push_back(makeField("f1", "c_field"));
    t.fields_.push_back(makeField("f2", "a_field"));
    t.fields_.push_back(makeField("f3", "b_field"));
    REQUIRE(repo.save(t).has_value());

    auto loaded = repo.findById(mondoc::TemplateId{"t1"});
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->fields_.size() == 3);
    REQUIRE(loaded->fields_[0].name_ == "c_field");
    REQUIRE(loaded->fields_[1].name_ == "a_field");
    REQUIRE(loaded->fields_[2].name_ == "b_field");
}
