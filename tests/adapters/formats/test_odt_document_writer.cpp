#include <catch2/catch_test_macros.hpp>

#include <pugixml.hpp>
#include <zip.h>

#include "odt_document_writer.hpp"
#include "domain/fill.hpp"
#include "domain/field.hpp"
#include "domain/template.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <string_view>
#include <vector>

using mondoc::adapters::formats::OdtDocumentWriter;
using mondoc::domain::Field;
using mondoc::domain::FieldOrigin;
using mondoc::domain::FieldType;
using mondoc::domain::Fill;
using mondoc::domain::Template;

namespace {

std::filesystem::path uniqueTempPath(std::string_view ext) {
    static std::mt19937_64 rng{std::random_device{}()};
    auto suffix = std::to_string(rng());
    auto path = std::filesystem::temp_directory_path()
               / ("mondoc_odt_writer_" + suffix + std::string{ext});
    std::error_code ec;
    std::filesystem::remove(path, ec);
    return path;
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

std::vector<unsigned char> readAllBytes(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    REQUIRE(in.good());
    return std::vector<unsigned char>{std::istreambuf_iterator<char>{in},
                                      std::istreambuf_iterator<char>{}};
}

std::string readContentXmlFrom(const std::filesystem::path& path) {
    int err = 0;
    zip_t* zf = zip_open(path.string().c_str(), ZIP_RDONLY, &err);
    REQUIRE(zf != nullptr);
    zip_stat_t st;
    zip_stat_init(&st);
    REQUIRE(zip_stat(zf, "content.xml", 0, &st) == 0);
    zip_file_t* entry = zip_fopen(zf, "content.xml", 0);
    REQUIRE(entry != nullptr);
    std::string xml;
    xml.resize(static_cast<std::size_t>(st.size));
    REQUIRE(zip_fread(entry, xml.data(), st.size) == static_cast<zip_int64_t>(st.size));
    zip_fclose(entry);
    zip_discard(zf);
    return xml;
}

struct TempFile {
    std::filesystem::path path;
    explicit TempFile(std::filesystem::path p) : path(std::move(p)) {}
    ~TempFile() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
};

constexpr std::string_view kFormControlXml = R"XML(<?xml version="1.0"?>
<office:document-content
    xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0"
    xmlns:form="urn:oasis:names:tc:opendocument:xmlns:form:1.0"
    xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0">
  <office:body><office:text>
    <office:forms>
      <form:form form:name="Form1">
        <form:text form:name="customer_name" form:current-value=""/>
      </form:form>
    </office:forms>
  </office:text></office:body>
</office:document-content>)XML";

}  // namespace

TEST_CASE("[EXPO-02] OdtDocumentWriter: does not mutate template file",
          "[formats.odt_writer]") {
    TempFile srcTmp{uniqueTempPath(".odt")};
    writeMinimalOdt(srcTmp.path, kFormControlXml);

    auto origBytes = readAllBytes(srcTmp.path);

    Template tpl;
    tpl.id_            = mondoc::TemplateId{"test-id"};
    tpl.name_          = "test";
    tpl.source_format_ = "odt";
    tpl.source_path_   = srcTmp.path;

    TempFile destTmp{uniqueTempPath(".odt")};
    OdtDocumentWriter writer;
    auto result = writer.write(tpl, {}, destTmp.path);
    REQUIRE(result.has_value());

    auto afterBytes = readAllBytes(srcTmp.path);
    REQUIRE(origBytes == afterBytes);
}

TEST_CASE("[EXPO-02] OdtDocumentWriter: applies form-control fill via form:current-value",
          "[formats.odt_writer]") {
    constexpr std::string_view xml = R"XML(<?xml version="1.0"?>
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

    TempFile srcTmp{uniqueTempPath(".odt")};
    writeMinimalOdt(srcTmp.path, xml);

    Field f;
    f.id_     = mondoc::FieldId{"field-1"};
    f.name_   = "customer_name";
    f.type_   = FieldType::Text;
    f.origin_ = FieldOrigin::FormControl;

    Template tpl;
    tpl.id_            = mondoc::TemplateId{"tpl-1"};
    tpl.name_          = "test";
    tpl.source_format_ = "odt";
    tpl.source_path_   = srcTmp.path;
    tpl.fields_        = {f};

    Fill fill;
    fill.field_id_      = mondoc::FieldId{"field-1"};
    fill.current_value_ = "Acme Corp";

    TempFile destTmp{uniqueTempPath(".odt")};
    OdtDocumentWriter writer;
    auto result = writer.write(tpl, {fill}, destTmp.path);
    REQUIRE(result.has_value());

    auto destXml = readContentXmlFrom(destTmp.path);
    REQUIRE(destXml.find("Acme Corp") != std::string::npos);
}

TEST_CASE("[EXPO-02] OdtDocumentWriter: applies placeholder substitution in text:p",
          "[formats.odt_writer]") {
    constexpr std::string_view xml = R"XML(<?xml version="1.0"?>
<office:document-content
    xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0"
    xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0">
  <office:body><office:text>
    <text:p>Dear {{customer_name}}, welcome.</text:p>
  </office:text></office:body>
</office:document-content>)XML";

    TempFile srcTmp{uniqueTempPath(".odt")};
    writeMinimalOdt(srcTmp.path, xml);

    Field f;
    f.id_     = mondoc::FieldId{"field-2"};
    f.name_   = "customer_name";
    f.type_   = FieldType::Text;
    f.origin_ = FieldOrigin::Placeholder;

    Template tpl;
    tpl.id_            = mondoc::TemplateId{"tpl-2"};
    tpl.name_          = "test2";
    tpl.source_format_ = "odt";
    tpl.source_path_   = srcTmp.path;
    tpl.fields_        = {f};

    Fill fill;
    fill.field_id_      = mondoc::FieldId{"field-2"};
    fill.current_value_ = "Jane Smith";

    TempFile destTmp{uniqueTempPath(".odt")};
    OdtDocumentWriter writer;
    auto result = writer.write(tpl, {fill}, destTmp.path);
    REQUIRE(result.has_value());

    auto destXml = readContentXmlFrom(destTmp.path);
    REQUIRE(destXml.find("Jane Smith") != std::string::npos);
    REQUIRE(destXml.find("{{customer_name}}") == std::string::npos);
}
