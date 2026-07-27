#include <catch2/catch_test_macros.hpp>

#include "plain_text_extractor.hpp"
#include "mondoc/error.hpp"

#include <filesystem>
#include <string>
#include <string_view>

#include "support/temp_files.hpp"
#include "support/zip_fixtures.hpp"

using mondoc::adapters::formats::extractPlainText;

namespace {

using mondoc::tests_support::TempFile;
using mondoc::tests_support::writeMinimalOdt;
using mondoc::tests_support::writeMinimalDocx;

std::filesystem::path uniqueTempPath(const std::string& suffix) {
    return mondoc::tests_support::uniqueTempPath("mondoc_test_", suffix);
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
    mondoc::tests_support::writeFile(tmp.path, "# Heading\nbody");

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
    mondoc::tests_support::writeFile(tmp.path, "not a zip file at all");

    CHECK_FALSE(extractPlainText(tmp.path).has_value());
}

TEST_CASE("extractPlainText: unsupported extension is InvalidArgument",
          "[formats.plain_text_extractor]") {
    auto r = extractPlainText(std::filesystem::path{"foo.png"});
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().kind() == mondoc::Error::Kind::InvalidArgument);
}
