#include <catch2/catch_test_macros.hpp>

#include "detail/zip_util.hpp"

#include <zip.h>

#include <filesystem>
#include <random>
#include <string>

using mondoc::adapters::formats::detail::readZipEntry;

namespace {

std::filesystem::path uniqueTempZipPath() {
    static std::mt19937_64 rng{std::random_device{}()};
    auto suffix = std::to_string(rng());
    auto path = std::filesystem::temp_directory_path()
                / ("mondoc_test_zip_util_" + suffix + ".zip");
    std::error_code ec;
    std::filesystem::remove(path, ec);
    return path;
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

TEST_CASE("readZipEntry reads full entry and enforces cap", "[formats.zip_util]") {
    TempFile tmp{uniqueTempZipPath()};
    const std::string big(2 * 1024 * 1024, 'a');

    {
        int err = 0;
        zip_t* zf = zip_open(tmp.path.string().c_str(), ZIP_CREATE | ZIP_TRUNCATE, &err);
        REQUIRE(zf != nullptr);
        zip_source_t* src = zip_source_buffer(zf, big.data(), big.size(), 0);
        REQUIRE(src != nullptr);
        REQUIRE(zip_file_add(zf, "big.txt", src, ZIP_FL_OVERWRITE) >= 0);
        REQUIRE(zip_close(zf) == 0);
    }

    int err = 0;
    zip_t* za = zip_open(tmp.path.string().c_str(), ZIP_RDONLY, &err);
    REQUIRE(za != nullptr);

    auto full = readZipEntry(za, "big.txt", 50ULL * 1024 * 1024);
    REQUIRE(full.has_value());
    REQUIRE(full->size() == big.size());
    REQUIRE(*full == big);

    auto rejected = readZipEntry(za, "big.txt", 1024);
    REQUIRE_FALSE(rejected.has_value());

    zip_discard(za);
}

TEST_CASE("readZipEntry reports missing entry", "[formats.zip_util]") {
    TempFile tmp{uniqueTempZipPath()};
    {
        int err = 0;
        zip_t* zf = zip_open(tmp.path.string().c_str(), ZIP_CREATE | ZIP_TRUNCATE, &err);
        REQUIRE(zf != nullptr);
        const std::string body = "hello";
        zip_source_t* src = zip_source_buffer(zf, body.data(), body.size(), 0);
        REQUIRE(src != nullptr);
        REQUIRE(zip_file_add(zf, "present.txt", src, ZIP_FL_OVERWRITE) >= 0);
        REQUIRE(zip_close(zf) == 0);
    }

    int err = 0;
    zip_t* za = zip_open(tmp.path.string().c_str(), ZIP_RDONLY, &err);
    REQUIRE(za != nullptr);

    auto missing = readZipEntry(za, "nope.txt", 1024);
    REQUIRE_FALSE(missing.has_value());

    zip_discard(za);
}
