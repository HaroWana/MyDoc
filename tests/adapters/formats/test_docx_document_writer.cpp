#include <catch2/catch_test_macros.hpp>

#include <pugixml.hpp>
#include <zip.h>

#include "docx_document_writer.hpp"
#include "domain/fill.hpp"
#include "domain/template.hpp"

#include "support/temp_files.hpp"
#include "support/zip_fixtures.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

using mondoc::adapters::formats::DocxDocumentWriter;
using mondoc::domain::Field;
using mondoc::domain::FieldType;
using mondoc::domain::Fill;
using mondoc::domain::Template;

namespace {

constexpr std::string_view kContentTypesXml = R"XML(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">
<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>
<Default Extension="xml" ContentType="application/xml"/>
<Override PartName="/word/document.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml"/>
</Types>
)XML";

constexpr std::string_view kRootRelsXml = R"XML(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="word/document.xml"/>
</Relationships>
)XML";

std::filesystem::path uniqueTempPath(std::string_view label) {
    return mondoc::tests_support::uniqueTempPath(
        "mondoc_writer_" + std::string{label} + "_", ".docx");
}

void writeMinimalDocx(const std::filesystem::path& path,
                      std::string_view documentXml) {
    mondoc::tests_support::writeZipEntries(path, {
        {"[Content_Types].xml", kContentTypesXml},
        {"_rels/.rels",         kRootRelsXml},
        {"word/document.xml",   documentXml},
    });
}

std::vector<unsigned char> readAllBytes(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    REQUIRE(in.good());
    return std::vector<unsigned char>{std::istreambuf_iterator<char>{in},
                                      std::istreambuf_iterator<char>{}};
}

uint64_t fnv1a(const std::vector<unsigned char>& bytes) {
    uint64_t h = 1469598103934665603ULL;
    for (auto b : bytes) {
        h ^= b;
        h *= 1099511628211ULL;
    }
    return h;
}

std::string readDocumentXmlFrom(const std::filesystem::path& path) {
    int err = 0;
    zip_t* zf = zip_open(path.string().c_str(), ZIP_RDONLY, &err);
    REQUIRE(zf != nullptr);
    zip_stat_t st;
    zip_stat_init(&st);
    REQUIRE(zip_stat(zf, "word/document.xml", 0, &st) == 0);
    zip_file_t* entry = zip_fopen(zf, "word/document.xml", 0);
    REQUIRE(entry != nullptr);
    std::string xml;
    xml.resize(static_cast<std::size_t>(st.size));
    REQUIRE(zip_fread(entry, xml.data(), st.size) == static_cast<zip_int64_t>(st.size));
    zip_fclose(entry);
    zip_discard(zf);
    return xml;
}

using mondoc::tests_support::TempFile;

}  // namespace

TEST_CASE("[TST-15] DocxDocumentWriter: rejects empty source_path_ with invalidArgument",
          "[formats.docx_writer][tst-15]") {
    Template tpl;
    tpl.id_            = mondoc::TemplateId{"empty-src"};
    tpl.name_          = "test";
    tpl.source_format_ = "docx";

    TempFile destFile{uniqueTempPath("out")};
    DocxDocumentWriter writer;
    auto result = writer.write(tpl, {}, destFile.path);
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().kind() == mondoc::Error::Kind::InvalidArgument);
}

TEST_CASE("[TST-15] DocxDocumentWriter: rejects a corrupt (non-zip) template file",
          "[formats.docx_writer][tst-15]") {
    TempFile templateFile{uniqueTempPath("tpl")};
    {
        std::ofstream out(templateFile.path, std::ios::binary);
        out << "not a zip file at all";
    }

    Template tpl;
    tpl.id_            = mondoc::TemplateId{"corrupt-src"};
    tpl.name_          = "test";
    tpl.source_format_ = "docx";
    tpl.source_path_   = templateFile.path;

    TempFile destFile{uniqueTempPath("out")};
    DocxDocumentWriter writer;
    auto result = writer.write(tpl, {}, destFile.path);
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().kind() == mondoc::Error::Kind::Generic);
}

TEST_CASE("DocxDocumentWriter: original template is byte-identical after a successful write",
          "[formats.docx_writer]") {
    TempFile templateFile{uniqueTempPath("tpl")};
    TempFile destFile{uniqueTempPath("out")};

    constexpr std::string_view xml = R"XML(<?xml version="1.0"?>
<w:document xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main">
  <w:body><w:p><w:r><w:t>Hello {{name}}</w:t></w:r></w:p></w:body>
</w:document>)XML";
    writeMinimalDocx(templateFile.path, xml);

    const auto preBytes = readAllBytes(templateFile.path);
    const auto preHash  = fnv1a(preBytes);

    Template tpl;
    tpl.id_            = mondoc::TemplateId{"t1"};
    tpl.name_          = "tpl";
    tpl.source_format_ = "docx";
    tpl.source_path_   = templateFile.path;
    Field f;
    f.id_ = mondoc::FieldId{"f1"};
    f.name_ = "name";
    f.type_ = FieldType::Text;
    tpl.fields_.push_back(f);

    std::vector<Fill> fills;
    Fill fill;
    fill.field_id_      = mondoc::FieldId{"f1"};
    fill.current_value_ = "World";
    fills.push_back(fill);

    auto result = DocxDocumentWriter{}.write(tpl, fills, destFile.path);
    REQUIRE(result.has_value());

    const auto postBytes = readAllBytes(templateFile.path);
    const auto postHash  = fnv1a(postBytes);
    REQUIRE(postHash == preHash);
    REQUIRE(postBytes.size() == preBytes.size());
}

TEST_CASE("DocxDocumentWriter: <w:sdt> content control receives fill value",
          "[formats.docx_writer]") {
    TempFile templateFile{uniqueTempPath("tpl")};
    TempFile destFile{uniqueTempPath("out")};

    constexpr std::string_view xml = R"XML(<?xml version="1.0"?>
<w:document xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main"><w:body><w:p><w:sdt><w:sdtPr><w:alias w:val="customer_name"/></w:sdtPr><w:sdtContent><w:r><w:t>placeholder</w:t></w:r></w:sdtContent></w:sdt></w:p></w:body></w:document>)XML";
    writeMinimalDocx(templateFile.path, xml);

    Template tpl;
    tpl.id_            = mondoc::TemplateId{"t1"};
    tpl.name_          = "tpl";
    tpl.source_format_ = "docx";
    tpl.source_path_   = templateFile.path;
    Field f;
    f.id_   = mondoc::FieldId{"f1"};
    f.name_ = "customer_name";
    f.type_ = FieldType::Text;
    tpl.fields_.push_back(f);

    std::vector<Fill> fills;
    Fill fill;
    fill.field_id_      = mondoc::FieldId{"f1"};
    fill.current_value_ = "Acme Corp";
    fills.push_back(fill);

    auto result = DocxDocumentWriter{}.write(tpl, fills, destFile.path);
    REQUIRE(result.has_value());

    const std::string outXml = readDocumentXmlFrom(destFile.path);
    pugi::xml_document doc;
    REQUIRE(doc.load_buffer(outXml.data(), outXml.size()).status == pugi::status_ok);

    pugi::xml_node sdt = doc.select_node("//w:sdt").node();
    REQUIRE(sdt);
    pugi::xml_node content = sdt.child("w:sdtContent");
    REQUIRE(content);

    int runCount = 0;
    pugi::xml_node firstRun;
    for (pugi::xml_node run : content.children("w:r")) {
        ++runCount;
        if (!firstRun) firstRun = run;
    }
    REQUIRE(runCount == 1);
    pugi::xml_node t = firstRun.child("w:t");
    REQUIRE(t);
    REQUIRE(std::string{t.child_value()} == "Acme Corp");
}

TEST_CASE("DocxDocumentWriter: placeholder runs replaced; unfilled placeholders kept verbatim",
          "[formats.docx_writer]") {
    TempFile templateFile{uniqueTempPath("tpl")};
    TempFile destFile{uniqueTempPath("out")};

    constexpr std::string_view xml = R"XML(<?xml version="1.0"?>
<w:document xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main"><w:body><w:p><w:r><w:t xml:space="preserve">Hello {{first_name}}, signed [DATE_SIGNED].</w:t></w:r></w:p></w:body></w:document>)XML";
    writeMinimalDocx(templateFile.path, xml);

    Template tpl;
    tpl.id_            = mondoc::TemplateId{"t1"};
    tpl.name_          = "tpl";
    tpl.source_format_ = "docx";
    tpl.source_path_   = templateFile.path;
    Field f1;
    f1.id_ = mondoc::FieldId{"f1"};
    f1.name_ = "first_name";
    f1.type_ = FieldType::Text;
    Field f2;
    f2.id_ = mondoc::FieldId{"f2"};
    f2.name_ = "date_signed";
    f2.type_ = FieldType::Text;
    tpl.fields_.push_back(f1);
    tpl.fields_.push_back(f2);

    std::vector<Fill> fills;
    Fill fill;
    fill.field_id_      = mondoc::FieldId{"f1"};
    fill.current_value_ = "Jane";
    fills.push_back(fill);

    auto result = DocxDocumentWriter{}.write(tpl, fills, destFile.path);
    REQUIRE(result.has_value());

    const std::string outXml = readDocumentXmlFrom(destFile.path);
    pugi::xml_document doc;
    REQUIRE(doc.load_buffer(outXml.data(), outXml.size()).status == pugi::status_ok);

    pugi::xml_node para = doc.select_node("//w:p").node();
    REQUIRE(para);

    std::string text;
    for (pugi::xml_node run : para.children("w:r")) {
        for (pugi::xml_node t : run.children("w:t")) {
            text += t.child_value();
        }
    }
    REQUIRE(text == "Hello Jane, signed [DATE_SIGNED].");
}

TEST_CASE("DocxDocumentWriter: substitution never re-scans inserted values",
          "[formats.docx_writer]") {
    TempFile templateFile{uniqueTempPath("tpl")};
    TempFile destFile{uniqueTempPath("out")};

    constexpr std::string_view xml = R"XML(<?xml version="1.0"?>
<w:document xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main"><w:body><w:p><w:r><w:t xml:space="preserve">Name: {{name}} Note: [note]</w:t></w:r></w:p></w:body></w:document>)XML";
    writeMinimalDocx(templateFile.path, xml);

    Template tpl;
    tpl.id_            = mondoc::TemplateId{"t1"};
    tpl.name_          = "tpl";
    tpl.source_format_ = "docx";
    tpl.source_path_   = templateFile.path;
    Field f1;
    f1.id_ = mondoc::FieldId{"f1"};
    f1.name_ = "name";
    f1.type_ = FieldType::Text;
    Field f2;
    f2.id_ = mondoc::FieldId{"f2"};
    f2.name_ = "note";
    f2.type_ = FieldType::Text;
    tpl.fields_.push_back(f1);
    tpl.fields_.push_back(f2);

    std::vector<Fill> fills;
    Fill fillName;
    fillName.field_id_      = mondoc::FieldId{"f1"};
    fillName.current_value_ = "John [note] Smith";
    fills.push_back(fillName);
    Fill fillNote;
    fillNote.field_id_      = mondoc::FieldId{"f2"};
    fillNote.current_value_ = "SECRET";
    fills.push_back(fillNote);

    auto result = DocxDocumentWriter{}.write(tpl, fills, destFile.path);
    REQUIRE(result.has_value());

    const std::string outXml = readDocumentXmlFrom(destFile.path);
    pugi::xml_document doc;
    REQUIRE(doc.load_buffer(outXml.data(), outXml.size()).status == pugi::status_ok);

    pugi::xml_node para = doc.select_node("//w:p").node();
    REQUIRE(para);

    std::string text;
    for (pugi::xml_node run : para.children("w:r")) {
        for (pugi::xml_node t : run.children("w:t")) {
            text += t.child_value();
        }
    }
    REQUIRE(text == "Name: John [note] Smith Note: SECRET");
}

TEST_CASE("block-level SDT fill wraps run in w:p", "[formats.docx_writer]") {
    TempFile templateFile{uniqueTempPath("tpl")};
    TempFile destFile{uniqueTempPath("out")};

    constexpr std::string_view xml = R"XML(<?xml version="1.0"?>
<w:document xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main"><w:body><w:sdt><w:sdtPr><w:alias w:val="notes"/></w:sdtPr><w:sdtContent><w:p><w:r><w:t>placeholder</w:t></w:r></w:p></w:sdtContent></w:sdt></w:body></w:document>)XML";
    writeMinimalDocx(templateFile.path, xml);

    Template tpl;
    tpl.id_            = mondoc::TemplateId{"t1"};
    tpl.name_          = "tpl";
    tpl.source_format_ = "docx";
    tpl.source_path_   = templateFile.path;
    Field f;
    f.id_   = mondoc::FieldId{"f1"};
    f.name_ = "notes";
    f.type_ = FieldType::Paragraph;
    tpl.fields_.push_back(f);

    std::vector<Fill> fills;
    Fill fill;
    fill.field_id_      = mondoc::FieldId{"f1"};
    fill.current_value_ = "Some block text";
    fills.push_back(fill);

    auto result = DocxDocumentWriter{}.write(tpl, fills, destFile.path);
    REQUIRE(result.has_value());

    const std::string outXml = readDocumentXmlFrom(destFile.path);
    pugi::xml_document doc;
    REQUIRE(doc.load_buffer(outXml.data(), outXml.size()).status == pugi::status_ok);

    pugi::xml_node content = doc.select_node("//w:sdtContent").node();
    REQUIRE(content);

    pugi::xml_node firstChild = content.first_child();
    REQUIRE(firstChild);
    REQUIRE(std::string_view{firstChild.name()} == "w:p");

    pugi::xml_node run = firstChild.child("w:r");
    REQUIRE(run);
    pugi::xml_node t = run.child("w:t");
    REQUIRE(t);
    REQUIRE(std::string{t.child_value()} == "Some block text");
}

TEST_CASE("checkbox SDT fill sets w14:checked", "[formats.docx_writer]") {
    constexpr std::string_view xml = R"XML(<?xml version="1.0"?>
<w:document xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main" xmlns:w14="http://schemas.microsoft.com/office/word/2010/wordml"><w:body><w:sdt><w:sdtPr><w:alias w:val="agree"/><w14:checkbox><w14:checked w14:val="0"/></w14:checkbox></w:sdtPr><w:sdtContent><w:r><w:t>placeholder</w:t></w:r></w:sdtContent></w:sdt></w:body></w:document>)XML";

    auto runWith = [&](std::string_view fillValue,
                       const char* expectedVal,
                       const char* expectedGlyph) {
        TempFile templateFile{uniqueTempPath("tpl")};
        TempFile destFile{uniqueTempPath("out")};
        writeMinimalDocx(templateFile.path, xml);

        Template tpl;
        tpl.id_            = mondoc::TemplateId{"t1"};
        tpl.name_          = "tpl";
        tpl.source_format_ = "docx";
        tpl.source_path_   = templateFile.path;
        Field f;
        f.id_   = mondoc::FieldId{"f1"};
        f.name_ = "agree";
        f.type_ = FieldType::Checkbox;
        tpl.fields_.push_back(f);

        std::vector<Fill> fills;
        Fill fill;
        fill.field_id_      = mondoc::FieldId{"f1"};
        fill.current_value_ = std::string{fillValue};
        fills.push_back(fill);

        auto result = DocxDocumentWriter{}.write(tpl, fills, destFile.path);
        REQUIRE(result.has_value());

        const std::string outXml = readDocumentXmlFrom(destFile.path);
        pugi::xml_document doc;
        REQUIRE(doc.load_buffer(outXml.data(), outXml.size()).status == pugi::status_ok);

        pugi::xml_node checked = doc.select_node("//w14:checked").node();
        REQUIRE(checked);
        REQUIRE(std::string{checked.attribute("w14:val").value()} == expectedVal);

        pugi::xml_node content = doc.select_node("//w:sdtContent").node();
        REQUIRE(content);
        pugi::xml_node t = content.child("w:r").child("w:t");
        REQUIRE(t);
        REQUIRE(std::string{t.child_value()} == expectedGlyph);
    };

    runWith("true", "1", "\xE2\x98\x92");
    runWith("false", "0", "\xE2\x98\x90");
}
