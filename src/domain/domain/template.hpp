#pragma once
#include <filesystem>
#include <string>
#include <vector>
#include "mondoc/id.hpp"
#include "field.hpp"

namespace mondoc::domain {

struct Template {
    TemplateId id_;
    std::string name_;
    std::string source_format_;
    std::vector<Field> fields_;
    std::filesystem::path source_path_;
};

}  // namespace mondoc::domain
