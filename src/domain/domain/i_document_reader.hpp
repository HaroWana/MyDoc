#pragma once
#include <filesystem>
#include "mondoc/expected.hpp"
#include "mondoc/error.hpp"
#include "template.hpp"

namespace mondoc::domain {

class IDocumentReader {
public:
    virtual ~IDocumentReader() = default;
    virtual std::expected<Template, mondoc::Error>
        read(const std::filesystem::path& path) = 0;
};

}  // namespace mondoc::domain
