#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include "domain/field.hpp"

namespace mondoc::adapters::formats {

// Maps a frame drawn on a preview PDF to a text anchor in the template's
// plain text. nullopt when no text lies near the frame (image regions).
std::optional<mondoc::domain::TextLocation>
anchorForPreviewRect(const std::filesystem::path& previewPdf,
                     const mondoc::domain::PdfLocation& frame,
                     const std::string& plainText);

}  // namespace mondoc::adapters::formats
