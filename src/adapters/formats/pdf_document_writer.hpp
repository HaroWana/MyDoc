#pragma once

#include <filesystem>
#include <vector>

#include "domain/template.hpp"
#include "domain/fill.hpp"
#include "mondoc/error.hpp"
#include "mondoc/expected.hpp"

namespace mondoc::adapters::formats {

class PdfDocumentWriter {
public:
    mondoc::expected<void, mondoc::Error>
    write(const mondoc::domain::Template& tpl,
          const std::vector<mondoc::domain::Fill>& fills,
          const std::filesystem::path& dest);
};

}  // namespace mondoc::adapters::formats
