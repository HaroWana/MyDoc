#pragma once
#include <filesystem>
#include <vector>
#include "mondoc/expected.hpp"
#include "mondoc/error.hpp"
#include "template.hpp"
#include "fill.hpp"

namespace mondoc::domain {

class IDocumentWriter {
public:
    virtual ~IDocumentWriter() = default;
    virtual mondoc::expected<void, mondoc::Error>
        write(const Template& tpl,
              const std::vector<Fill>& fills,
              const std::filesystem::path& dest) = 0;
};

}  // namespace mondoc::domain
