#pragma once
#include <filesystem>
#include <vector>
#include "domain/i_document_writer.hpp"
#include "domain/template.hpp"
#include "domain/fill.hpp"
#include "mondoc/error.hpp"
#include "mondoc/expected.hpp"

namespace mondoc::adapters::formats {

class OdtDocumentWriter : public mondoc::domain::IDocumentWriter {
public:
    mondoc::expected<void, mondoc::Error>
    write(const mondoc::domain::Template& tpl,
          const std::vector<mondoc::domain::Fill>& fills,
          const std::filesystem::path& dest) override;
};

}  // namespace mondoc::adapters::formats
