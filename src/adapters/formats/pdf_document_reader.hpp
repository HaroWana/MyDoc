#pragma once
#include <filesystem>
#include "domain/i_document_reader.hpp"
#include "mondoc/error.hpp"
#include "mondoc/expected.hpp"
#include "domain/template.hpp"

namespace mondoc::adapters::formats {

class PdfDocumentReader : public mondoc::domain::IDocumentReader {
public:
    mondoc::expected<mondoc::domain::Template, mondoc::Error>
    read(const std::filesystem::path& path) override;
};

}  // namespace mondoc::adapters::formats
