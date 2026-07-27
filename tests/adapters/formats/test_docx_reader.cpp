#include <catch2/catch_test_macros.hpp>

#include <zip.h>

#include "docx_document_reader.hpp"
#include "domain/field.hpp"
#include "domain/template.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <string_view>

using namespace mondoc::adapters::formats;
using mondoc::domain::FieldType;

namespace {

// Minimal valid Content_Types XML — required for any OOXML package to be
// schema-valid; the reader does not parse it, but real DOCX files have it.
constexpr std::string_view kContentTypesXml = R"XML(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">
<Default Extension="xml" ContentType="application/xml"/>
<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>
<Override PartName="/word/document.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml"/>
</Types>
)XML";

std::filesystem::path uniqueTempDocxPath() {
    static std::mt19937_64 rng{std::random_device{}()};
    auto suffix = std::to_string(rng());
    auto path = std::filesystem::temp_directory_path()
                / ("mondoc_test_" + suffix + ".docx");
    std::error_code ec;
    std::filesystem::remove(path, ec);
    return path;
}

// Build a real ZIP archive at `path` containing [Content_Types].xml and
// word/document.xml — enough for the reader to treat it as a DOCX.
void writeMinimalDocx(const std::filesystem::path& path,
                      std::string_view documentXml) {
    int err = 0;
    zip_t* zf = zip_open(path.string().c_str(),
                         ZIP_CREATE | ZIP_TRUNCATE, &err);
    REQUIRE(zf != nullptr);

    auto addEntry = [&](const char* name, std::string_view body) {
        zip_source_t* src = zip_source_buffer(zf, body.data(), body.size(), 0);
        REQUIRE(src != nullptr);
        REQUIRE(zip_file_add(zf, name, src, ZIP_FL_OVERWRITE) >= 0);
    };

    addEntry("[Content_Types].xml", kContentTypesXml);
    addEntry("word/document.xml",  documentXml);

    REQUIRE(zip_close(zf) == 0);
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

TEST_CASE("DocxDocumentReader: rejects non-.docx extension",
          "[formats.docx]") {
    DocxDocumentReader reader;
    auto result = reader.read(std::filesystem::path{"foo.txt"});
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().kind() == mondoc::Error::Kind::InvalidArgument);
}

TEST_CASE("DocxDocumentReader: returns error for corrupted ZIP",
          "[formats.docx]") {
    TempFile tmp{uniqueTempDocxPath()};
    {
        std::ofstream os(tmp.path, std::ios::binary);
        os << "this is not a zip file at all";
    }

    DocxDocumentReader reader;
    auto result = reader.read(tmp.path);
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().kind() == mondoc::Error::Kind::Generic);
}

TEST_CASE("DocxDocumentReader: extracts w:sdt field with w:alias",
          "[formats.docx]") {
    TempFile tmp{uniqueTempDocxPath()};
    constexpr std::string_view xml = R"XML(<?xml version="1.0"?>
<w:document xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main">
  <w:body>
    <w:sdt>
      <w:sdtPr>
        <w:alias w:val="Customer Name"/>
        <w:tag w:val="customer_tag"/>
        <w:text/>
      </w:sdtPr>
      <w:sdtContent><w:p><w:r><w:t>placeholder</w:t></w:r></w:p></w:sdtContent>
    </w:sdt>
  </w:body>
</w:document>)XML";
    writeMinimalDocx(tmp.path, xml);

    DocxDocumentReader reader;
    auto result = reader.read(tmp.path);
    REQUIRE(result.has_value());
    REQUIRE(result->source_format_ == "docx");
    REQUIRE(result->fields_.size() == 1);
    REQUIRE(result->fields_[0].name_ == "customer_name");
    REQUIRE(result->fields_[0].type_ == FieldType::Text);
}

TEST_CASE("DocxDocumentReader: w:sdt with w:date infers FieldType::Date",
          "[formats.docx]") {
    TempFile tmp{uniqueTempDocxPath()};
    constexpr std::string_view xml = R"XML(<?xml version="1.0"?>
<w:document xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main">
  <w:body>
    <w:sdt>
      <w:sdtPr>
        <w:alias w:val="signed_on"/>
        <w:date/>
      </w:sdtPr>
      <w:sdtContent><w:p><w:r><w:t>2025-01-01</w:t></w:r></w:p></w:sdtContent>
    </w:sdt>
  </w:body>
</w:document>)XML";
    writeMinimalDocx(tmp.path, xml);

    DocxDocumentReader reader;
    auto result = reader.read(tmp.path);
    REQUIRE(result.has_value());
    REQUIRE(result->fields_.size() == 1);
    REQUIRE(result->fields_[0].name_ == "signed_on");
    REQUIRE(result->fields_[0].type_ == FieldType::Date);
}

TEST_CASE("DocxDocumentReader: w:sdt with w14:checkbox infers FieldType::Checkbox",
          "[formats.docx]") {
    TempFile tmp{uniqueTempDocxPath()};
    constexpr std::string_view xml = R"XML(<?xml version="1.0"?>
<w:document xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main" xmlns:w14="http://schemas.microsoft.com/office/word/2010/wordml">
  <w:body>
    <w:sdt>
      <w:sdtPr>
        <w:alias w:val="agree_to_terms"/>
        <w14:checkbox><w14:checked w14:val="0"/></w14:checkbox>
      </w:sdtPr>
      <w:sdtContent><w:r><w:t>&#9744;</w:t></w:r></w:sdtContent>
    </w:sdt>
  </w:body>
</w:document>)XML";
    writeMinimalDocx(tmp.path, xml);

    DocxDocumentReader reader;
    auto result = reader.read(tmp.path);
    REQUIRE(result.has_value());
    REQUIRE(result->fields_.size() == 1);
    REQUIRE(result->fields_[0].name_ == "agree_to_terms");
    REQUIRE(result->fields_[0].type_ == FieldType::Checkbox);
}

TEST_CASE("DocxDocumentReader: extracts {{placeholder}} from paragraph text",
          "[formats.docx]") {
    TempFile tmp{uniqueTempDocxPath()};
    // Split the placeholder across two w:r/w:t runs to exercise run-merge.
    constexpr std::string_view xml = R"XML(<?xml version="1.0"?>
<w:document xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main">
  <w:body>
    <w:p>
      <w:r><w:t xml:space="preserve">Hello {{first</w:t></w:r>
      <w:r><w:t xml:space="preserve">_name}}, welcome.</w:t></w:r>
    </w:p>
  </w:body>
</w:document>)XML";
    writeMinimalDocx(tmp.path, xml);

    DocxDocumentReader reader;
    auto result = reader.read(tmp.path);
    REQUIRE(result.has_value());
    REQUIRE(result->fields_.size() == 1);
    REQUIRE(result->fields_[0].name_ == "first_name");
    REQUIRE(result->fields_[0].type_ == FieldType::Text);
}

TEST_CASE("DocxDocumentReader: deduplicates — content control type wins over placeholder",
          "[formats.docx]") {
    TempFile tmp{uniqueTempDocxPath()};
    // The same normalized name "signed_on" appears as a w:date sdt
    // and as a {{signed_on}} placeholder in a paragraph. The sdt entry
    // (FieldType::Date) must win over the placeholder (FieldType::Text).
    constexpr std::string_view xml = R"XML(<?xml version="1.0"?>
<w:document xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main">
  <w:body>
    <w:sdt>
      <w:sdtPr>
        <w:alias w:val="signed_on"/>
        <w:date/>
      </w:sdtPr>
      <w:sdtContent><w:p><w:r><w:t>2025</w:t></w:r></w:p></w:sdtContent>
    </w:sdt>
    <w:p><w:r><w:t>See {{signed_on}} above.</w:t></w:r></w:p>
  </w:body>
</w:document>)XML";
    writeMinimalDocx(tmp.path, xml);

    DocxDocumentReader reader;
    auto result = reader.read(tmp.path);
    REQUIRE(result.has_value());
    REQUIRE(result->fields_.size() == 1);
    REQUIRE(result->fields_[0].name_ == "signed_on");
    REQUIRE(result->fields_[0].type_ == FieldType::Date);
}

TEST_CASE("DocxDocumentReader: normalizes field name — spaces to underscores, lowercase",
          "[formats.docx]") {
    TempFile tmp{uniqueTempDocxPath()};
    constexpr std::string_view xml = R"XML(<?xml version="1.0"?>
<w:document xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main">
  <w:body>
    <w:p><w:r><w:t>{{ First Name }}</w:t></w:r></w:p>
  </w:body>
</w:document>)XML";
    writeMinimalDocx(tmp.path, xml);

    DocxDocumentReader reader;
    auto result = reader.read(tmp.path);
    REQUIRE(result.has_value());
    REQUIRE(result->fields_.size() == 1);
    REQUIRE(result->fields_[0].name_ == "first_name");
}

TEST_CASE("DocxDocumentReader: matches all three placeholder patterns",
          "[formats.docx]") {
    TempFile tmp{uniqueTempDocxPath()};
    // <FIELD> appears as &lt;FIELD&gt; in raw XML; pugixml decodes it before
    // our regex sees the text.
    constexpr std::string_view xml = R"XML(<?xml version="1.0"?>
<w:document xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main">
  <w:body>
    <w:p><w:r><w:t>{{double}} and [SQUARE] and &lt;ANGLE&gt; here.</w:t></w:r></w:p>
  </w:body>
</w:document>)XML";
    writeMinimalDocx(tmp.path, xml);

    DocxDocumentReader reader;
    auto result = reader.read(tmp.path);
    REQUIRE(result.has_value());
    REQUIRE(result->fields_.size() == 3);

    bool sawDouble = false, sawSquare = false, sawAngle = false;
    for (const auto& f : result->fields_) {
        if (f.name_ == "double") sawDouble = true;
        if (f.name_ == "square") sawSquare = true;
        if (f.name_ == "angle")  sawAngle  = true;
    }
    REQUIRE(sawDouble);
    REQUIRE(sawSquare);
    REQUIRE(sawAngle);
}

TEST_CASE("DocxDocumentReader: missing word/document.xml returns generic error",
          "[formats.docx]") {
    TempFile tmp{uniqueTempDocxPath()};
    // Build a ZIP with only [Content_Types].xml — no word/document.xml.
    int err = 0;
    zip_t* zf = zip_open(tmp.path.string().c_str(),
                         ZIP_CREATE | ZIP_TRUNCATE, &err);
    REQUIRE(zf != nullptr);
    zip_source_t* src = zip_source_buffer(zf, kContentTypesXml.data(),
                                          kContentTypesXml.size(), 0);
    REQUIRE(src != nullptr);
    REQUIRE(zip_file_add(zf, "[Content_Types].xml", src, ZIP_FL_OVERWRITE) >= 0);
    REQUIRE(zip_close(zf) == 0);

    DocxDocumentReader reader;
    auto result = reader.read(tmp.path);
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().kind() == mondoc::Error::Kind::Generic);
}

TEST_CASE("DocxDocumentReader: sets template name from filename stem",
          "[formats.docx]") {
    auto path = std::filesystem::temp_directory_path() / "invoice_v3.docx";
    {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
    TempFile tmp{path};
    constexpr std::string_view xml = R"XML(<?xml version="1.0"?>
<w:document xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main">
  <w:body><w:p><w:r><w:t>no fields</w:t></w:r></w:p></w:body>
</w:document>)XML";
    writeMinimalDocx(tmp.path, xml);

    DocxDocumentReader reader;
    auto result = reader.read(tmp.path);
    REQUIRE(result.has_value());
    REQUIRE(result->name_ == "invoice_v3");
    REQUIRE(result->source_format_ == "docx");
    REQUIRE(result->fields_.empty());
}
