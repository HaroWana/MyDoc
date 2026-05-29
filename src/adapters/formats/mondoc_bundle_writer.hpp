#pragma once

#include <filesystem>

#include "domain/template.hpp"
#include "mondoc/error.hpp"
#include "mondoc/expected.hpp"

namespace mondoc::adapters::formats {

class MondocBundleWriter {
public:
    mondoc::expected<void, mondoc::Error>
    write(const mondoc::domain::Template& tpl, const std::filesystem::path& dest);
};

}  // namespace mondoc::adapters::formats
