#include <catch2/catch_test_macros.hpp>

#include "detail/zip_util.hpp"

#include <zip.h>

#include <cstdint>
#include <filesystem>
#include <string>

#include "support/temp_files.hpp"
#include "support/zip_fixtures.hpp"

using mondoc::adapters::formats::detail::readZipEntry;

namespace {

std::filesystem::path uniqueTempZipPath() {
    return mondoc::tests_support::uniqueTempPath("mondoc_test_zip_util_", ".zip");
}

using mondoc::tests_support::TempFile;
using mondoc::tests_support::writeZipEntries;

}  // namespace

TEST_CASE("readZipEntry reads full entry and enforces cap", "[formats.zip_util]") {
    TempFile tmp{uniqueTempZipPath()};
    const std::string big(2 * 1024 * 1024, 'a');

    writeZipEntries(tmp.path, {{"big.txt", big}});

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

TEST_CASE("[TST-11] readZipEntry rejects a zip-bomb entry before reading its content",
          "[formats.zip_util][tst-11]") {
    TempFile tmp{uniqueTempZipPath()};
    constexpr std::uint64_t kMaxBytes = 50ULL * 1024 * 1024;
    // Highly compressible (single repeated byte) so DEFLATE shrinks it to a
    // tiny archive on disk, while the entry still declares > 50 MB
    // uncompressed — the actual "zip bomb" shape this guard exists for.
    const std::string bomb(static_cast<std::size_t>(kMaxBytes) + 1024, 'a');

    writeZipEntries(tmp.path, {{"bomb.txt", bomb}});

    REQUIRE(std::filesystem::file_size(tmp.path) < 1024ULL * 1024);

    int err = 0;
    zip_t* za = zip_open(tmp.path.string().c_str(), ZIP_RDONLY, &err);
    REQUIRE(za != nullptr);

    auto result = readZipEntry(za, "bomb.txt", kMaxBytes);
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().message().find("exceeds size limit") != std::string::npos);

    zip_discard(za);
}

TEST_CASE("readZipEntry reports missing entry", "[formats.zip_util]") {
    TempFile tmp{uniqueTempZipPath()};
    writeZipEntries(tmp.path, {{"present.txt", "hello"}});

    int err = 0;
    zip_t* za = zip_open(tmp.path.string().c_str(), ZIP_RDONLY, &err);
    REQUIRE(za != nullptr);

    auto missing = readZipEntry(za, "nope.txt", 1024);
    REQUIRE_FALSE(missing.has_value());

    zip_discard(za);
}
