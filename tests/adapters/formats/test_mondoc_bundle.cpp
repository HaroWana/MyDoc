#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <zip.h>

#include <filesystem>
#include <fstream>
#include <ios>
#include <string>
#include <system_error>

#include "domain/field.hpp"
#include "domain/template.hpp"
#include "mondoc_bundle_reader.hpp"
#include "mondoc_bundle_writer.hpp"

using mondoc::adapters::formats::MondocBundleReader;
using mondoc::adapters::formats::MondocBundleWriter;
using mondoc::domain::Field;
using mondoc::domain::FieldLocation;
using mondoc::domain::FieldOrigin;
using mondoc::domain::FieldType;
using mondoc::domain::PdfLocation;
using mondoc::domain::Template;

namespace {

std::filesystem::path tempBundlePath(const std::string& suffix) {
    return std::filesystem::temp_directory_path()
           / ("mondoc_test_bundle_" + suffix + ".mondoc");
}

struct TempSourceFile {
    std::filesystem::path path;
    explicit TempSourceFile(const std::string& filename)
        : path(std::filesystem::temp_directory_path() / filename) {
        std::ofstream f(path, std::ios::binary);
        f << "fake source content";
    }
    ~TempSourceFile() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
};

struct TempFile {
    std::filesystem::path path;
    explicit TempFile(std::filesystem::path p) : path(std::move(p)) {}
    ~TempFile() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
};

}  // namespace

TEST_CASE("MondocBundle: round-trip preserves template name and field schema [phase05][adapters.formats.mondoc]") {
    TempSourceFile src{"mondoc_rt_source.docx"};
    Template tpl;
    tpl.id_ = mondoc::TemplateId{"test-tpl-id"};
    tpl.name_ = "Contract Template";
    tpl.source_format_ = "docx";
    tpl.source_path_ = src.path;
    Field f;
    f.id_ = mondoc::FieldId{"field-01"};
    f.name_ = "client_name";
    f.type_ = FieldType::Text;
    f.origin_ = FieldOrigin::Placeholder;
    tpl.fields_.push_back(f);

    TempFile out{tempBundlePath("roundtrip")};
    MondocBundleWriter writer;
    auto writeResult = writer.write(tpl, out.path);
    REQUIRE(writeResult.has_value());

    MondocBundleReader reader;
    auto readResult = reader.read(out.path);
    REQUIRE(readResult.has_value());
    REQUIRE(readResult->name_ == "Contract Template");
    REQUIRE(readResult->source_format_ == "docx");
    REQUIRE(readResult->fields_.size() == 1);
    REQUIRE(readResult->fields_[0].name_ == "client_name");
    REQUIRE(readResult->fields_[0].type_ == FieldType::Text);
    REQUIRE(readResult->fields_[0].origin_ == FieldOrigin::Placeholder);
}

TEST_CASE("MondocBundle: malformed bundle returns Error [phase05][adapters.formats.mondoc]") {
    TempFile bad{tempBundlePath("bad")};
    {
        std::ofstream f(bad.path, std::ios::binary);
        f << "not a zip file at all";
    }
    MondocBundleReader reader;
    auto result = reader.read(bad.path);
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("MondocBundle: path traversal in source_filename rejected [phase05][adapters.formats.mondoc]") {
    TempFile out{tempBundlePath("traversal")};
    std::string manifest =
        R"({"mondoc_version":1,"name":"x","source_format":"docx",)"
        R"("source_filename":"../etc/passwd","exported_at":"2026-01-01T00:00:00Z",)"
        R"("fields":[]})";
    {
        int ec = 0;
        auto u8 = out.path.u8string();
        std::string nativePath(reinterpret_cast<const char*>(u8.data()), u8.size());
        zip_t* za = zip_open(nativePath.c_str(), ZIP_CREATE | ZIP_TRUNCATE, &ec);
        REQUIRE(za != nullptr);
        zip_source_t* src = zip_source_buffer(za, manifest.data(), manifest.size(), 0);
        REQUIRE(src != nullptr);
        REQUIRE(zip_file_add(za, "manifest.json", src, ZIP_FL_OVERWRITE | ZIP_FL_ENC_UTF_8) >= 0);
        REQUIRE(zip_close(za) >= 0);
    }

    MondocBundleReader reader;
    auto result = reader.read(out.path);
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().message().find("invalid") != std::string::npos);
}

TEST_CASE("MondocBundle: missing mondoc_version rejected [phase05][adapters.formats.mondoc]") {
    TempFile out{tempBundlePath("no_version")};
    std::string manifest =
        R"({"name":"x","source_format":"docx","source_filename":"x.docx","fields":[]})";
    {
        int ec = 0;
        auto u8 = out.path.u8string();
        std::string nativePath(reinterpret_cast<const char*>(u8.data()), u8.size());
        zip_t* za = zip_open(nativePath.c_str(), ZIP_CREATE | ZIP_TRUNCATE, &ec);
        REQUIRE(za != nullptr);
        zip_source_t* src = zip_source_buffer(za, manifest.data(), manifest.size(), 0);
        REQUIRE(src != nullptr);
        REQUIRE(zip_file_add(za, "manifest.json", src, ZIP_FL_OVERWRITE | ZIP_FL_ENC_UTF_8) >= 0);
        REQUIRE(zip_close(za) >= 0);
    }

    MondocBundleReader reader;
    auto result = reader.read(out.path);
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().message().find("mondoc_version") != std::string::npos);
}

TEST_CASE("MondocBundle: rejects source file larger than 50MB [phase05][adapters.formats.mondoc]") {
    TempFile src{std::filesystem::temp_directory_path() / "mondoc_test_bundle_huge.docx"};
    {
        std::ofstream f(src.path, std::ios::binary);
        f << "fake source content";
    }
    std::error_code resizeEc;
    std::filesystem::resize_file(src.path, 51ULL * 1024 * 1024, resizeEc);
    REQUIRE_FALSE(resizeEc);

    Template tpl;
    tpl.id_ = mondoc::TemplateId{"huge-tpl"};
    tpl.name_ = "Huge Template";
    tpl.source_format_ = "docx";
    tpl.source_path_ = src.path;

    TempFile out{tempBundlePath("huge")};
    MondocBundleWriter writer;
    auto result = writer.write(tpl, out.path);
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("MondocBundle: rejects source file named manifest.json [phase05][adapters.formats.mondoc]") {
    TempFile src{std::filesystem::temp_directory_path() / "manifest.json"};
    {
        std::ofstream f(src.path, std::ios::binary);
        f << "fake source content";
    }

    Template tpl;
    tpl.id_ = mondoc::TemplateId{"manifest-tpl"};
    tpl.name_ = "Manifest Named Template";
    tpl.source_format_ = "docx";
    tpl.source_path_ = src.path;

    TempFile out{tempBundlePath("manifest_named")};
    MondocBundleWriter writer;
    auto result = writer.write(tpl, out.path);
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().kind() == mondoc::Error::Kind::InvalidArgument);
}

TEST_CASE("MondocBundle: PdfLocation round-trips through bundle [phase05][adapters.formats.mondoc]") {
    TempSourceFile src{"mondoc_loc_source.pdf"};
    Template tpl;
    tpl.id_ = mondoc::TemplateId{"loc-tpl"};
    tpl.name_ = "PDF Template";
    tpl.source_format_ = "pdf";
    tpl.source_path_ = src.path;
    Field f;
    f.id_ = mondoc::FieldId{"loc-field"};
    f.name_ = "signature_box";
    f.type_ = FieldType::Text;
    f.origin_ = FieldOrigin::Unknown;
    PdfLocation pdfLoc{0, 0.1, 0.2, 0.3, 0.4};
    f.location_ = FieldLocation{pdfLoc, std::nullopt};
    tpl.fields_.push_back(f);

    TempFile out{tempBundlePath("loc_rt")};
    MondocBundleWriter writer;
    REQUIRE(writer.write(tpl, out.path).has_value());

    MondocBundleReader reader;
    auto result = reader.read(out.path);
    REQUIRE(result.has_value());
    REQUIRE(result->fields_.size() == 1);
    REQUIRE(result->fields_[0].location_.has_value());
    REQUIRE(result->fields_[0].location_->pdf.has_value());
    REQUIRE(result->fields_[0].location_->pdf->page_index == 0);
    REQUIRE(result->fields_[0].location_->pdf->x == Catch::Approx(0.1));
    REQUIRE(result->fields_[0].location_->pdf->y == Catch::Approx(0.2));
    REQUIRE(result->fields_[0].location_->pdf->w == Catch::Approx(0.3));
    REQUIRE(result->fields_[0].location_->pdf->h == Catch::Approx(0.4));
}
