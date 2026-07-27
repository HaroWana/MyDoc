#include <catch2/catch_test_macros.hpp>

#include <zip.h>

#include "plain_text_extractor.hpp"
#include "mondoc/error.hpp"

#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <string_view>

using mondoc::adapters::formats::extractPlainText;

namespace {

struct TempFile {
    std::filesystem::path path;
    explicit TempFile(std::filesystem::path p) : path(std::move(p)) {}
    ~TempFile() { std::error_code ec; std::filesystem::remove(path, ec); }
};

std::filesystem::path uniqueTempPath(const std::string& suffix) {
    static std::mt19937_64 rng{std::random_device{}()};
    auto id = std::to_string(rng());
    return std::filesystem::temp_directory_path() / ("mondoc_test_" + id + suffix);
}

void writeMinimalOdt(const std::filesystem::path& path,
                     std::string_view contentXml) {
    int err = 0;
    zip_t* zf = zip_open(path.string().c_str(), ZIP_CREATE | ZIP_TRUNCATE, &err);
    REQUIRE(zf != nullptr);
    zip_source_t* src = zip_source_buffer(zf, contentXml.data(), contentXml.size(), 0);
    REQUIRE(src != nullptr);
    REQUIRE(zip_file_add(zf, "content.xml", src, ZIP_FL_OVERWRITE) >= 0);
    REQUIRE(zip_close(zf) == 0);
}

void writeMinimalDocx(const std::filesystem::path& path,
                      std::string_view documentXml) {
    int err = 0;
    zip_t* zf = zip_open(path.string().c_str(), ZIP_CREATE | ZIP_TRUNCATE, &err);
    REQUIRE(zf != nullptr);
    zip_source_t* src = zip_source_buffer(zf, documentXml.data(), documentXml.size(), 0);
    REQUIRE(src != nullptr);
    REQUIRE(zip_file_add(zf, "word/document.xml", src, ZIP_FL_OVERWRITE) >= 0);
    REQUIRE(zip_close(zf) == 0);
}

constexpr std::string_view kOdtSpanXml = R"XML(<?xml version="1.0"?>
<office:document-content
    xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0"
    xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0">
  <office:body><office:text><text:p>Hello <text:span>world</text:span> end</text:p></office:text></office:body>
</office:document-content>)XML";

constexpr std::string_view kOdtWhitespaceXml = R"XML(<?xml version="1.0"?>
<office:document-content
    xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0"
    xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0">
  <office:body><office:text><text:p>a<text:tab/>b<text:s text:c="3"/>c<text:line-break/>d</text:p></office:text></office:body>
</office:document-content>)XML";

constexpr std::string_view kDocxXml = R"XML(<?xml version="1.0"?>
<w:document xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main">
  <w:body>
    <w:p><w:r><w:t>First line</w:t></w:r></w:p>
    <w:p><w:r><w:t>Second </w:t></w:r><w:r><w:t>line</w:t></w:r></w:p>
  </w:body>
</w:document>)XML";

}  // namespace

TEST_CASE("extractPlainText: ODT spans are neither doubled nor dropped",
          "[formats.plain_text_extractor]") {
    TempFile tmp{uniqueTempPath(".odt")};
    writeMinimalOdt(tmp.path, kOdtSpanXml);

    auto r = extractPlainText(tmp.path);

    REQUIRE(r.has_value());
    CHECK(*r == "Hello world end\n");
}

TEST_CASE("extractPlainText: ODT whitespace elements map to characters",
          "[formats.plain_text_extractor]") {
    TempFile tmp{uniqueTempPath(".odt")};
    writeMinimalOdt(tmp.path, kOdtWhitespaceXml);

    auto r = extractPlainText(tmp.path);

    REQUIRE(r.has_value());
    CHECK(*r == "a\tb   c\nd\n");
}

TEST_CASE("extractPlainText: DOCX returns w:t text with paragraph breaks",
          "[formats.plain_text_extractor]") {
    TempFile tmp{uniqueTempPath(".docx")};
    writeMinimalDocx(tmp.path, kDocxXml);

    auto r = extractPlainText(tmp.path);

    REQUIRE(r.has_value());
    CHECK(*r == "First line\nSecond line");
}

TEST_CASE("extractPlainText: reads .txt and .md verbatim",
          "[formats.plain_text_extractor]") {
    TempFile tmp{uniqueTempPath(".md")};
    {
        std::ofstream out(tmp.path, std::ios::binary);
        out << "# Heading\nbody";
    }

    auto r = extractPlainText(tmp.path);

    REQUIRE(r.has_value());
    CHECK(*r == "# Heading\nbody");
}

TEST_CASE("extractPlainText: extraction errors are surfaced",
          "[formats.plain_text_extractor]") {
    CHECK_FALSE(extractPlainText(std::filesystem::path{"nope.docx"}).has_value());
    CHECK_FALSE(extractPlainText(std::filesystem::path{"nope.odt"}).has_value());
    CHECK_FALSE(extractPlainText(std::filesystem::path{"nope.pdf"}).has_value());
    CHECK_FALSE(extractPlainText(std::filesystem::path{"nope.txt"}).has_value());
}

TEST_CASE("extractPlainText: corrupt zip returns an error",
          "[formats.plain_text_extractor]") {
    TempFile tmp{uniqueTempPath(".docx")};
    {
        std::ofstream out(tmp.path, std::ios::binary);
        out << "not a zip file at all";
    }

    CHECK_FALSE(extractPlainText(tmp.path).has_value());
}

TEST_CASE("extractPlainText: unsupported extension is InvalidArgument",
          "[formats.plain_text_extractor]") {
    auto r = extractPlainText(std::filesystem::path{"foo.png"});
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().kind() == mondoc::Error::Kind::InvalidArgument);
}
