#include <catch2/catch_test_macros.hpp>

#include "preview_provider.hpp"

#include "support/temp_files.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <system_error>

using namespace mondoc::adapters::formats;
using mondoc::tests_support::TempFile;
using mondoc::tests_support::writeFile;

namespace {
std::filesystem::path uniqueTempPath(const std::string& ext) {
    return mondoc::tests_support::uniqueTempPath("mondoc_test_preview_", ext);
}
}  // namespace

TEST_CASE("previewPdfFor: pdf source is identity", "[formats.preview]") {
    TempFile src{uniqueTempPath(".pdf")};
    writeFile(src.path, "%PDF-1.4\n");
    auto r = previewPdfFor(src.path, "tpl1", std::filesystem::temp_directory_path(), {});
    REQUIRE(r.has_value());
    REQUIRE(r->pdf_ == src.path);
    REQUIRE_FALSE(r->regenerated_);
}

TEST_CASE("previewPdfFor: unsupported extension is an error", "[formats.preview]") {
    TempFile src{uniqueTempPath(".xyz")};
    writeFile(src.path, "x");
    auto r = previewPdfFor(src.path, "tpl1", std::filesystem::temp_directory_path(), {});
    REQUIRE_FALSE(r.has_value());
}

TEST_CASE("previewPdfFor: fresh sidecar reuses the cache without soffice", "[formats.preview]") {
    TempFile src{uniqueTempPath(".txt")};
    writeFile(src.path, "hello");
    const auto cacheDir = uniqueTempPath("");
    std::filesystem::create_directories(cacheDir);
    writeFile(cacheDir / "tpl1.pdf", "%PDF-1.4 cached\n");
    std::error_code ec;
    const auto size = std::filesystem::file_size(src.path, ec);
    const auto mtime = std::filesystem::last_write_time(src.path, ec)
                           .time_since_epoch().count();
    nlohmann::json sidecar{{"size", size}, {"mtime", mtime}};
    writeFile(cacheDir / "tpl1.json", sidecar.dump());

    // sofficePath deliberately bogus: a cache hit must not invoke it.
    auto r = previewPdfFor(src.path, "tpl1", cacheDir, "/nonexistent/soffice");
    REQUIRE(r.has_value());
    REQUIRE(r->pdf_ == cacheDir / "tpl1.pdf");
    REQUIRE_FALSE(r->regenerated_);
    std::filesystem::remove_all(cacheDir, ec);
}

TEST_CASE("previewPdfFor: unsafe templateId is rejected", "[formats.preview]") {
    TempFile src{uniqueTempPath(".txt")};
    writeFile(src.path, "hello");
    const auto cacheDir = std::filesystem::temp_directory_path();

    auto r1 = previewPdfFor(src.path, "../escape", cacheDir, {});
    REQUIRE_FALSE(r1.has_value());
    REQUIRE(r1.error().kind() == mondoc::Error::Kind::InvalidArgument);

    auto r2 = previewPdfFor(src.path, "tpl1; rm -rf /", cacheDir, {});
    REQUIRE_FALSE(r2.has_value());
    REQUIRE(r2.error().kind() == mondoc::Error::Kind::InvalidArgument);
}

TEST_CASE("previewPdfFor: cache hit works when cacheDir contains a space", "[formats.preview]") {
    TempFile src{uniqueTempPath(".txt")};
    writeFile(src.path, "hello");
    const auto cacheDir = uniqueTempPath("") += " with space";
    std::filesystem::create_directories(cacheDir);
    writeFile(cacheDir / "tpl1.pdf", "%PDF-1.4 cached\n");
    std::error_code ec;
    const auto size = std::filesystem::file_size(src.path, ec);
    const auto mtime = std::filesystem::last_write_time(src.path, ec)
                           .time_since_epoch().count();
    nlohmann::json sidecar{{"size", size}, {"mtime", mtime}};
    writeFile(cacheDir / "tpl1.json", sidecar.dump());

    auto r = previewPdfFor(src.path, "tpl1", cacheDir, "/nonexistent/soffice");
    REQUIRE(r.has_value());
    REQUIRE(r->pdf_ == cacheDir / "tpl1.pdf");
    REQUIRE_FALSE(r->regenerated_);
    std::filesystem::remove_all(cacheDir, ec);
}

TEST_CASE("previewPdfFor: cache hit works when source filename has shell metacharacters",
          "[formats.preview]") {
    const auto srcDir = uniqueTempPath("");
    std::filesystem::create_directories(srcDir);
    const auto srcPath = srcDir / "invoice`id`$(whoami).txt";
    writeFile(srcPath, "hello");
    const auto cacheDir = uniqueTempPath("");
    std::filesystem::create_directories(cacheDir);
    writeFile(cacheDir / "tpl1.pdf", "%PDF-1.4 cached\n");
    std::error_code ec;
    const auto size = std::filesystem::file_size(srcPath, ec);
    const auto mtime = std::filesystem::last_write_time(srcPath, ec)
                           .time_since_epoch().count();
    nlohmann::json sidecar{{"size", size}, {"mtime", mtime}};
    writeFile(cacheDir / "tpl1.json", sidecar.dump());

    // sofficePath deliberately bogus: a cache hit must not invoke it, so a
    // filename with `, $, or backslash metacharacters can't reach a shell.
    auto r = previewPdfFor(srcPath, "tpl1", cacheDir, "/nonexistent/soffice");
    REQUIRE(r.has_value());
    REQUIRE(r->pdf_ == cacheDir / "tpl1.pdf");
    REQUIRE_FALSE(r->regenerated_);
    std::filesystem::remove_all(srcDir, ec);
    std::filesystem::remove_all(cacheDir, ec);
}

TEST_CASE("previewPdfFor: missing soffice on a cold cache is a clear error", "[formats.preview]") {
    TempFile src{uniqueTempPath(".txt")};
    writeFile(src.path, "hello");
    const auto cacheDir = uniqueTempPath("");
    std::filesystem::create_directories(cacheDir);
    auto r = previewPdfFor(src.path, "tpl1", cacheDir, "/nonexistent/soffice");
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().message().find("LibreOffice") != std::string::npos);
    std::error_code ec;
    std::filesystem::remove_all(cacheDir, ec);
}

TEST_CASE("previewPdfFor: real conversion produces a PDF", "[formats.preview][lo]") {
    if (findLibreOffice().empty()) SKIP("LibreOffice not installed");
    TempFile src{uniqueTempPath(".txt")};
    writeFile(src.path, "hello preview world");
    const auto cacheDir = uniqueTempPath("");
    std::filesystem::create_directories(cacheDir);
    auto r = previewPdfFor(src.path, "tpl1", cacheDir, findLibreOffice());
    REQUIRE(r.has_value());
    REQUIRE(r->regenerated_);
    REQUIRE(std::filesystem::file_size(r->pdf_) > 100);
    // second call: cache hit
    auto r2 = previewPdfFor(src.path, "tpl1", cacheDir, findLibreOffice());
    REQUIRE(r2.has_value());
    REQUIRE_FALSE(r2->regenerated_);
    std::error_code ec;
    std::filesystem::remove_all(cacheDir, ec);
}
