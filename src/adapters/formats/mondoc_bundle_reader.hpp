#pragma once

#include <filesystem>

#include "domain/template.hpp"
#include "mondoc/error.hpp"
#include "mondoc/expected.hpp"

namespace mondoc::adapters::formats {

class MondocBundleReader {
public:
    mondoc::expected<mondoc::domain::Template, mondoc::Error>
    read(const std::filesystem::path& src);
};

}  // namespace mondoc::adapters::formats
