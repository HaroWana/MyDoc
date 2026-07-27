#include <catch2/catch_test_macros.hpp>

#include <zip.h>

#include "odt_document_reader.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <string_view>

using namespace mondoc::adapters::formats;

namespace {

struct TempFile {
    std::filesystem::path path;
    explicit TempFile(std::filesystem::path p) : path(std::move(p)) {}
    ~TempFile() { std::error_code ec; std::filesystem::remove(path, ec); }
};

std::filesystem::path uniqueTempOdtPath() {
    static std::mt19937_64 rng{std::random_device{}()};
    auto suffix = std::to_string(rng());
    return std::filesystem::temp_directory_path()
           / ("mondoc_test_" + suffix + ".odt");
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

}  // namespace

TEST_CASE("[TMPL-02] OdtDocumentReader: rejects file with wrong extension",
          "[formats.odt_reader]") {
    OdtDocumentReader reader;
    auto result = reader.read(std::filesystem::path{"foo.docx"});
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("[TMPL-02] OdtDocumentReader: returns error for corrupt zip",
          "[formats.odt_reader]") {
    TempFile f{uniqueTempOdtPath()};
    {
        std::ofstream out(f.path, std::ios::binary);
        out << "not a zip file at all";
    }
    OdtDocumentReader reader;
    auto result = reader.read(f.path);
    REQUIRE_FALSE(result.has_value());
}

namespace {

constexpr std::string_view kFormTextXml = R"XML(<?xml version="1.0"?>
<office:document-content
    xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0"
    xmlns:form="urn:oasis:names:tc:opendocument:xmlns:form:1.0"
    xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0">
  <office:body><office:text>
    <office:forms>
      <form:form form:name="F1">
        <form:text form:name="customer_name" form:current-value=""/>
      </form:form>
    </office:forms>
  </office:text></office:body>
</office:document-content>)XML";

constexpr std::string_view kPlaceholderXml = R"XML(<?xml version="1.0"?>
<office:document-content
    xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0"
    xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0">
  <office:body><office:text>
    <text:p>Hello {{first_name}}, your date is [DATE_SIGNED].</text:p>
  </office:text></office:body>
</office:document-content>)XML";

constexpr std::string_view kDedupXml = R"XML(<?xml version="1.0"?>
<office:document-content
    xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0"
    xmlns:form="urn:oasis:names:tc:opendocument:xmlns:form:1.0"
    xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0">
  <office:body><office:text>
    <office:forms>
      <form:form form:name="F1">
        <form:textarea form:name="address" form:current-value=""/>
      </form:form>
    </office:forms>
    <text:p>Fill in {{address}} here.</text:p>
  </office:text></office:body>
</office:document-content>)XML";

constexpr std::string_view kPlaceholderAfterSpanXml = R"XML(<?xml version="1.0"?>
<office:document-content
    xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0"
    xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0">
  <office:body><office:text>
    <text:p>Hello <text:span>styled</text:span> {{after_span}}</text:p>
  </office:text></office:body>
</office:document-content>)XML";

}  // namespace

TEST_CASE("[TMPL-02] OdtDocumentReader: extracts form:text field",
          "[formats.odt_reader]") {
    TempFile tmp{uniqueTempOdtPath()};
    writeMinimalOdt(tmp.path, kFormTextXml);
    OdtDocumentReader reader;
    auto result = reader.read(tmp.path);
    REQUIRE(result.has_value());
    REQUIRE(result->fields_.size() == 1);
    REQUIRE(result->fields_[0].name_ == "customer_name");
    REQUIRE(result->fields_[0].type_ == mondoc::domain::FieldType::Text);
    REQUIRE(result->fields_[0].origin_ == mondoc::domain::FieldOrigin::FormControl);
}

TEST_CASE("[TMPL-02] OdtDocumentReader: extracts {{placeholder}} field",
          "[formats.odt_reader]") {
    TempFile tmp{uniqueTempOdtPath()};
    writeMinimalOdt(tmp.path, kPlaceholderXml);
    OdtDocumentReader reader;
    auto result = reader.read(tmp.path);
    REQUIRE(result.has_value());
    auto& fields = result->fields_;
    auto it = std::find_if(fields.begin(), fields.end(),
        [](const auto& f){ return f.name_ == "first_name"; });
    REQUIRE(it != fields.end());
    REQUIRE(it->origin_ == mondoc::domain::FieldOrigin::Placeholder);
}

TEST_CASE("[TMPL-02] OdtDocumentReader: detects placeholder after an inline span",
          "[formats.odt_reader]") {
    TempFile tmp{uniqueTempOdtPath()};
    writeMinimalOdt(tmp.path, kPlaceholderAfterSpanXml);
    OdtDocumentReader reader;
    auto result = reader.read(tmp.path);
    REQUIRE(result.has_value());
    auto& fields = result->fields_;
    auto it = std::find_if(fields.begin(), fields.end(),
        [](const auto& f){ return f.name_ == "after_span"; });
    REQUIRE(it != fields.end());
    REQUIRE(it->origin_ == mondoc::domain::FieldOrigin::Placeholder);
}

TEST_CASE("[TMPL-02] OdtDocumentReader: form-control wins on dedup",
          "[formats.odt_reader]") {
    TempFile tmp{uniqueTempOdtPath()};
    writeMinimalOdt(tmp.path, kDedupXml);
    OdtDocumentReader reader;
    auto result = reader.read(tmp.path);
    REQUIRE(result.has_value());
    auto& fields = result->fields_;
    long count = std::count_if(fields.begin(), fields.end(),
        [](const auto& f){ return f.name_ == "address"; });
    REQUIRE(count == 1);
    auto it = std::find_if(fields.begin(), fields.end(),
        [](const auto& f){ return f.name_ == "address"; });
    REQUIRE(it != fields.end());
    REQUIRE(it->type_ == mondoc::domain::FieldType::Paragraph);
    REQUIRE(it->origin_ == mondoc::domain::FieldOrigin::FormControl);
}
