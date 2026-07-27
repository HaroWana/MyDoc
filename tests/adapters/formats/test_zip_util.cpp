#include <catch2/catch_test_macros.hpp>

#include "detail/zip_util.hpp"

#include <zip.h>

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
