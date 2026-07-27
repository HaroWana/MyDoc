#pragma once

#include "mondoc/error.hpp"
#include "mondoc/expected.hpp"

#include <filesystem>
#include <string>

namespace mondoc::adapters::formats {

// Extracts the plain text body of a source document. Dispatches on the
// lowercased extension: .docx, .odt, .pdf, .txt, .md. Any other extension is
// an InvalidArgument error; unreadable or malformed files yield a generic
// error.
mondoc::expected<std::string, mondoc::Error>
extractPlainText(const std::filesystem::path& src);

}  // namespace mondoc::adapters::formats
