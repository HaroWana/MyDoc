#include <catch2/catch_test_macros.hpp>

#include <zip.h>

#include "odt_document_reader.hpp"

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

TEST_CASE("[TMPL-02] OdtDocumentReader: extracts form:text field",
          "[formats.odt_reader]") {
    FAIL("not yet implemented");
}

TEST_CASE("[TMPL-02] OdtDocumentReader: extracts {{placeholder}} field",
          "[formats.odt_reader]") {
    FAIL("not yet implemented");
}

TEST_CASE("[TMPL-02] OdtDocumentReader: form-control wins on dedup",
          "[formats.odt_reader]") {
    FAIL("not yet implemented");
}
