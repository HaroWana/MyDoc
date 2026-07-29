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

TEST_CASE("SqliteTemplateRepository: FieldLocation PdfLocation round-trips [phase05][storage.template_repo]") {
    auto conn = SqliteConnection::open(":memory:");
    REQUIRE(conn.has_value());
    REQUIRE(runMigrations(*conn).has_value());
    SqliteTemplateRepository repo(*conn);

    auto t = makeTemplate("t1", "Invoice");
    auto f = makeField("f1", "signature");
    mondoc::domain::PdfLocation pl{0, 0.1, 0.2, 0.3, 0.4};
    f.location_ = mondoc::domain::FieldLocation{pl, std::nullopt};
    t.fields_.push_back(std::move(f));
    REQUIRE(repo.save(t).has_value());

    auto loaded = repo.findById(mondoc::TemplateId{"t1"});
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->fields_.size() == 1);
    REQUIRE(loaded->fields_[0].location_.has_value());
    REQUIRE(loaded->fields_[0].location_->pdf.has_value());
    REQUIRE_FALSE(loaded->fields_[0].location_->text.has_value());
    REQUIRE(loaded->fields_[0].location_->pdf->page_index == 0);
    REQUIRE(loaded->fields_[0].location_->pdf->x == 0.1);
    REQUIRE(loaded->fields_[0].location_->pdf->y == 0.2);
    REQUIRE(loaded->fields_[0].location_->pdf->w == 0.3);
    REQUIRE(loaded->fields_[0].location_->pdf->h == 0.4);
}

TEST_CASE("SqliteTemplateRepository: Field without location_ stores NULL in DB [phase05][storage.template_repo]") {
    auto conn = SqliteConnection::open(":memory:");
    REQUIRE(conn.has_value());
    REQUIRE(runMigrations(*conn).has_value());
    SqliteTemplateRepository repo(*conn);

    auto t = makeTemplate("t1", "Invoice");
    t.fields_.push_back(makeField("f1", "customer_name"));
    REQUIRE(repo.save(t).has_value());

    SQLite::Statement q(conn->raw(),
        "SELECT location_json FROM template_fields WHERE id = ?");
    q.bind(1, std::string{"f1"});
    REQUIRE(q.executeStep());
    REQUIRE(q.getColumn(0).isNull());

    auto loaded = repo.findById(mondoc::TemplateId{"t1"});
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->fields_.size() == 1);
    REQUIRE_FALSE(loaded->fields_[0].location_.has_value());
}

TEST_CASE("SqliteTemplateRepository: FieldLocation TextLocation round-trips [phase05][storage.template_repo]") {
    auto conn = SqliteConnection::open(":memory:");
    REQUIRE(conn.has_value());
    REQUIRE(runMigrations(*conn).has_value());
    SqliteTemplateRepository repo(*conn);

    auto t = makeTemplate("t1", "Memo");
    auto f = makeField("f1", "title");
    mondoc::domain::TextLocation tl{3, 12};
    f.location_ = mondoc::domain::FieldLocation{std::nullopt, tl};
    t.fields_.push_back(std::move(f));
    REQUIRE(repo.save(t).has_value());

    auto loaded = repo.findById(mondoc::TemplateId{"t1"});
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->fields_.size() == 1);
    REQUIRE(loaded->fields_[0].location_.has_value());
    REQUIRE_FALSE(loaded->fields_[0].location_->pdf.has_value());
    REQUIRE(loaded->fields_[0].location_->text.has_value());
    REQUIRE(loaded->fields_[0].location_->text->paragraph_index == 3);
    REQUIRE(loaded->fields_[0].location_->text->char_offset == 12);
}

TEST_CASE("SqliteTemplateRepository: FieldOrigin::Ai round-trips through storage",
          "[adapters.storage][template_repo][aifd-03][phase06]") {
    auto conn = SqliteConnection::open(":memory:");
    REQUIRE(conn.has_value());
    REQUIRE(runMigrations(*conn).has_value());
    SqliteTemplateRepository repo(*conn);

    auto t = makeTemplate("t1", "Invoice");
    auto f = makeField("f1", "detected_field");
    f.origin_ = mondoc::domain::FieldOrigin::Ai;
    t.fields_.push_back(std::move(f));
    REQUIRE(repo.save(t).has_value());

    auto loaded = repo.findById(mondoc::TemplateId{"t1"});
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->fields_.size() == 1);
    REQUIRE(loaded->fields_[0].origin_ == mondoc::domain::FieldOrigin::Ai);
}

TEST_CASE("SqliteTemplateRepository: origins persist as snake tokens",
          "[storage.template_repo]") {
    auto conn = SqliteConnection::open(":memory:");
    REQUIRE(conn.has_value());
    REQUIRE(runMigrations(*conn).has_value());
    SqliteTemplateRepository repo(*conn);

    auto t = makeTemplate("tpl-origin", "Origins");
    auto f = makeField("fld-origin", "x");
    f.origin_ = mondoc::domain::FieldOrigin::FormControl;
    t.fields_.push_back(std::move(f));
    REQUIRE(repo.save(t).has_value());

    SQLite::Statement q(conn->raw(),
        "SELECT origin FROM template_fields WHERE id = 'fld-origin'");
    REQUIRE(q.executeStep());
    REQUIRE(q.getColumn(0).getString() == "form_control");

    auto back = repo.findById(mondoc::TemplateId{"tpl-origin"});
    REQUIRE(back.has_value());
    REQUIRE(back->fields_.at(0).origin_ == mondoc::domain::FieldOrigin::FormControl);
}

TEST_CASE("SqliteTemplateRepository: listAll attaches fields to the right template",
          "[storage.template_repo]") {
    auto conn = SqliteConnection::open(":memory:");
    REQUIRE(conn.has_value());
    REQUIRE(runMigrations(*conn).has_value());
    SqliteTemplateRepository repo(*conn);

    auto a = makeTemplate("ta", "A");
    a.fields_.push_back(makeField("a1", "a_first"));
    a.fields_.push_back(makeField("a2", "a_second"));
    auto b = makeTemplate("tb", "B");
    b.fields_.push_back(makeField("b1", "b_only"));
    REQUIRE(repo.save(a).has_value());
    REQUIRE(repo.save(b).has_value());

    auto list = repo.listAll();
    REQUIRE(list.has_value());
    REQUIRE(list->size() == 2);
    REQUIRE((*list)[0].name_ == "A");
    REQUIRE((*list)[0].fields_.size() == 2);
    REQUIRE((*list)[0].fields_[0].name_ == "a_first");
    REQUIRE((*list)[0].fields_[1].name_ == "a_second");
    REQUIRE((*list)[1].name_ == "B");
    REQUIRE((*list)[1].fields_.size() == 1);
    REQUIRE((*list)[1].fields_[0].name_ == "b_only");
}

TEST_CASE("SqliteTemplateRepository: TextLocation range and excerpt round-trip",
          "[storage.template_repo][ui-8]") {
    auto conn = SqliteConnection::open(":memory:");
    REQUIRE(conn.has_value());
    REQUIRE(runMigrations(*conn).has_value());
    SqliteTemplateRepository repo(*conn);

    auto t = makeTemplate("t1", "Memo");
    auto f = makeField("f1", "title");
    mondoc::domain::TextLocation tl;
    tl.paragraph_index = 2;
    tl.char_offset = 10;
    tl.char_end = 25;
    tl.excerpt = "hello sele";
    f.location_ = mondoc::domain::FieldLocation{std::nullopt, tl};
    t.fields_.push_back(std::move(f));
    REQUIRE(repo.save(t).has_value());

    auto loaded = repo.findById(mondoc::TemplateId{"t1"});
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->fields_.at(0).location_.has_value());
    const auto& text = loaded->fields_.at(0).location_->text;
    REQUIRE(text.has_value());
    REQUIRE(text->paragraph_index == 2);
    REQUIRE(text->char_offset == 10);
    REQUIRE(text->char_end == 25);
    REQUIRE(text->excerpt == "hello sele");
}

TEST_CASE("SqliteTemplateRepository: co-resident pdf+text location round-trips",
          "[storage.template_repo][visual-fill]") {
    auto conn = SqliteConnection::open(":memory:");
    REQUIRE(conn.has_value());
    REQUIRE(runMigrations(*conn).has_value());
    SqliteTemplateRepository repo(*conn);

    auto t = makeTemplate("t1", "Dual");
    auto f = makeField("f1", "spot");
    mondoc::domain::PdfLocation pl{1, 0.1, 0.2, 0.3, 0.05};
    mondoc::domain::TextLocation tl{2, 10, 25, "hello"};
    f.location_ = mondoc::domain::FieldLocation{pl, tl};
    t.fields_.push_back(std::move(f));
    REQUIRE(repo.save(t).has_value());
    auto back = repo.findById(mondoc::TemplateId{"t1"});
    REQUIRE(back.has_value());
    const auto& loc = back->fields_.at(0).location_;
    REQUIRE(loc.has_value());
    REQUIRE(loc->pdf.has_value());
    REQUIRE(loc->text.has_value());
    REQUIRE(loc->pdf->page_index == 1);
    REQUIRE(loc->text->char_end == 25);
}

TEST_CASE("SqliteTemplateRepository: legacy location_json without char_end parses",
          "[storage.template_repo][ui-8]") {
    auto conn = SqliteConnection::open(":memory:");
    REQUIRE(conn.has_value());
    REQUIRE(runMigrations(*conn).has_value());
    SqliteTemplateRepository repo(*conn);

    auto t = makeTemplate("t1", "Memo");
    t.fields_.push_back(makeField("f1", "title"));
    REQUIRE(repo.save(t).has_value());

    SQLite::Statement upd(conn->raw(),
        "UPDATE template_fields SET location_json ="
        " '{\"type\":\"text\",\"paragraph_index\":1,\"char_offset\":5}'"
        " WHERE id = 'f1'");
    upd.exec();

    auto loaded = repo.findById(mondoc::TemplateId{"t1"});
    REQUIRE(loaded.has_value());
    const auto& text = loaded->fields_.at(0).location_->text;
    REQUIRE(text.has_value());
    REQUIRE(text->char_offset == 5);
    REQUIRE(text->char_end == 0);
    REQUIRE(text->excerpt.empty());
}
