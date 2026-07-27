#pragma once

#include <zip.h>

#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace mondoc::tests_support {

// Writes a ZIP archive at `path` containing the given (entryName, body) pairs.
inline void writeZipEntries(
    const std::filesystem::path& path,
    const std::vector<std::pair<std::string, std::string_view>>& entries) {
    int err = 0;
    zip_t* zf = zip_open(path.string().c_str(), ZIP_CREATE | ZIP_TRUNCATE, &err);
    REQUIRE(zf != nullptr);
    for (const auto& [name, body] : entries) {
        zip_source_t* src = zip_source_buffer(zf, body.data(), body.size(), 0);
        REQUIRE(src != nullptr);
        REQUIRE(zip_file_add(zf, name.c_str(), src, ZIP_FL_OVERWRITE) >= 0);
    }
    REQUIRE(zip_close(zf) == 0);
}

// A DOCX containing only word/document.xml — enough for any reader/extractor
// that only looks at the main document part.
inline void writeMinimalDocx(const std::filesystem::path& path,
                             std::string_view documentXml) {
    writeZipEntries(path, {{"word/document.xml", documentXml}});
}

// An ODT containing only content.xml.
inline void writeMinimalOdt(const std::filesystem::path& path,
                            std::string_view contentXml) {
    writeZipEntries(path, {{"content.xml", contentXml}});
}

}  // namespace mondoc::tests_support
