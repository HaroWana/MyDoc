#pragma once

#include <filesystem>
#include <string>

#include "mondoc/error.hpp"
#include "mondoc/expected.hpp"

namespace mondoc::adapters::formats {

// Absolute path to the soffice binary: the override if it names an existing
// file, else a PATH search for "soffice" then "libreoffice". Empty when not found.
std::filesystem::path findLibreOffice(const std::filesystem::path& override = {});

struct PreviewResult {
    std::filesystem::path pdf;   // the preview PDF to render
    bool regenerated = false;    // true when a stale cache was rebuilt this call
};

// .pdf sources are returned as-is (regenerated=false, no cache entry).
// Other supported sources are converted with LibreOffice into
// cacheDir/<templateId>.pdf, with sidecar cacheDir/<templateId>.json holding
// {"size":N,"mtime":N} of the source at conversion time; a matching sidecar
// short-circuits to the cached file.
mondoc::expected<PreviewResult, mondoc::Error>
previewPdfFor(const std::filesystem::path& source,
              const std::string& templateId,
              const std::filesystem::path& cacheDir,
              const std::filesystem::path& sofficePath);

}  // namespace mondoc::adapters::formats
