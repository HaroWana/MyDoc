#pragma once
#include <filesystem>
#include <memory>
#include <string_view>
#include "domain/i_document_reader.hpp"
#include "domain/i_document_writer.hpp"

namespace mondoc::adapters::formats {

// nullptr when the extension names no supported source format.
std::unique_ptr<mondoc::domain::IDocumentReader>
readerForPath(const std::filesystem::path& path);

// ext must be a lowercase extension including the dot, e.g. ".docx".
// nullptr when no writer exists for the extension.
std::unique_ptr<mondoc::domain::IDocumentWriter>
writerForExtension(std::string_view ext);

}  // namespace mondoc::adapters::formats
