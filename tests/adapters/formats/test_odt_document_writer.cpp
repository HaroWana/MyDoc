#include <catch2/catch_test_macros.hpp>

#include <zip.h>

#include "odt_document_writer.hpp"

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
           / ("mondoc_odt_writer_" + suffix + ".odt");
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

TEST_CASE("[EXPO-02] OdtDocumentWriter: does not mutate template file",
          "[formats.odt_writer]") {
    FAIL("not yet implemented");
}

TEST_CASE("[EXPO-02] OdtDocumentWriter: applies form-control fill via form:current-value",
          "[formats.odt_writer]") {
    FAIL("not yet implemented");
}

TEST_CASE("[EXPO-02] OdtDocumentWriter: applies placeholder substitution in text:p",
          "[formats.odt_writer]") {
    FAIL("not yet implemented");
}
