#pragma once

#include <filesystem>

#include "domain/template.hpp"
#include "mondoc/error.hpp"
#include "mondoc/expected.hpp"

namespace mondoc::adapters::formats {

class MondocBundleReader {
public:
    // The returned Template's source_path_ is the bare bundle entry name (e.g.
    // "document.docx"), not a filesystem path — it still needs to be resolved
    // against an extracted-source directory by the caller (TemplateService)
    // before the document can be opened. Full inversion of this contract
    // (returning an already-extracted path) is deferred.
    mondoc::expected<mondoc::domain::Template, mondoc::Error>
    read(const std::filesystem::path& src);
};

}  // namespace mondoc::adapters::formats
