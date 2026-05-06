#include <catch2/catch_test_macros.hpp>

#include "plain_text_document_reader.hpp"
#include "domain/field.hpp"
#include "domain/template.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <utility>

using namespace mondoc::adapters::formats;
using mondoc::domain::FieldType;

namespace {

std::filesystem::path uniqueTempPath(const std::string& ext) {
    static std::mt19937_64 rng{std::random_device{}()};
    auto suffix = std::to_string(rng()) + "_" +
                  std::to_string(std::chrono::steady_clock::now()
                                     .time_since_epoch().count());
    auto path = std::filesystem::temp_directory_path()
                / ("mondoc_test_plain_" + suffix + ext);
    std::error_code ec;
    std::filesystem::remove(path, ec);
    return path;
}

void writeFile(const std::filesystem::path& path, const std::string& body) {
    std::ofstream f(path, std::ios::binary);
    REQUIRE(f.is_open());
    f << body;
}

struct TempFile {
    std::filesystem::path path;
    explicit TempFile(std::filesystem::path p) : path(std::move(p)) {}
    ~TempFile() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
};

}  // namespace

TEST_CASE("PlainTextDocumentReader: rejects unsupported extension",
          "[formats.plain_text]") {
    PlainTextDocumentReader reader;
    auto result = reader.read(std::filesystem::path{"foo.docx"});
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().kind() == mondoc::Error::Kind::InvalidArgument);
}

TEST_CASE("PlainTextDocumentReader: returns error for missing file",
          "[formats.plain_text]") {
    PlainTextDocumentReader reader;
    auto result = reader.read(std::filesystem::path{"/nonexistent/path/missing.txt"});
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().kind() == mondoc::Error::Kind::Generic);
}

TEST_CASE("PlainTextDocumentReader: extracts {{double_brace}} placeholder",
          "[formats.plain_text]") {
    TempFile tmp{uniqueTempPath(".txt")};
    writeFile(tmp.path, "Hello {{customer_name}}!");

    PlainTextDocumentReader reader;
    auto result = reader.read(tmp.path);
    REQUIRE(result.has_value());
    REQUIRE(result->source_format_ == "txt");
    REQUIRE(result->fields_.size() == 1);
    REQUIRE(result->fields_[0].name_ == "customer_name");
    REQUIRE(result->fields_[0].type_ == FieldType::Text);
}

TEST_CASE("PlainTextDocumentReader: extracts [SQUARE_BRACKET] placeholder",
          "[formats.plain_text]") {
    TempFile tmp{uniqueTempPath(".txt")};
    writeFile(tmp.path, "[DATE_SIGNED]");

    PlainTextDocumentReader reader;
    auto result = reader.read(tmp.path);
    REQUIRE(result.has_value());
    REQUIRE(result->fields_.size() == 1);
    REQUIRE(result->fields_[0].name_ == "date_signed");
    REQUIRE(result->fields_[0].type_ == FieldType::Text);
}

TEST_CASE("PlainTextDocumentReader: extracts <ANGLE_BRACKET> placeholder",
          "[formats.plain_text]") {
    TempFile tmp{uniqueTempPath(".txt")};
    writeFile(tmp.path, "<Company>");

    PlainTextDocumentReader reader;
    auto result = reader.read(tmp.path);
    REQUIRE(result.has_value());
    REQUIRE(result->fields_.size() == 1);
    REQUIRE(result->fields_[0].name_ == "company");
    REQUIRE(result->fields_[0].type_ == FieldType::Text);
}

TEST_CASE("PlainTextDocumentReader: deduplicates repeated placeholder",
          "[formats.plain_text]") {
    TempFile tmp{uniqueTempPath(".txt")};
    writeFile(tmp.path, "{{name}} and {{name}}");

    PlainTextDocumentReader reader;
    auto result = reader.read(tmp.path);
    REQUIRE(result.has_value());
    REQUIRE(result->fields_.size() == 1);
    REQUIRE(result->fields_[0].name_ == "name");
}

TEST_CASE("PlainTextDocumentReader: normalizes name — spaces to underscores, lowercase",
          "[formats.plain_text]") {
    TempFile tmp{uniqueTempPath(".txt")};
    writeFile(tmp.path, "{{ First Name }}");

    PlainTextDocumentReader reader;
    auto result = reader.read(tmp.path);
    REQUIRE(result.has_value());
    REQUIRE(result->fields_.size() == 1);
    REQUIRE(result->fields_[0].name_ == "first_name");
}

TEST_CASE("PlainTextDocumentReader: accepts .md extension",
          "[formats.plain_text]") {
    TempFile tmp{uniqueTempPath(".md")};
    writeFile(tmp.path, "{{title}}");

    PlainTextDocumentReader reader;
    auto result = reader.read(tmp.path);
    REQUIRE(result.has_value());
    REQUIRE(result->source_format_ == "md");
    REQUIRE(result->fields_.size() == 1);
    REQUIRE(result->fields_[0].name_ == "title");
}

TEST_CASE("PlainTextDocumentReader: multiple different placeholders in one file",
          "[formats.plain_text]") {
    TempFile tmp{uniqueTempPath(".txt")};
    writeFile(tmp.path, "{{a}} [BB] <CC>");

    PlainTextDocumentReader reader;
    auto result = reader.read(tmp.path);
    REQUIRE(result.has_value());
    REQUIRE(result->fields_.size() == 3);

    bool sawA = false, sawBb = false, sawCc = false;
    for (const auto& f : result->fields_) {
        if (f.name_ == "a")  sawA  = true;
        if (f.name_ == "bb") sawBb = true;
        if (f.name_ == "cc") sawCc = true;
    }
    REQUIRE(sawA);
    REQUIRE(sawBb);
    REQUIRE(sawCc);
}
