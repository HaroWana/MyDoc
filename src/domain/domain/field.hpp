#pragma once
#include <string>
#include "mondoc/id.hpp"

namespace mondoc::domain {

enum class FieldType {
    Text, Paragraph, Number, Date, Checkbox, Dropdown
};

struct Field {
    FieldId id_;
    std::string name_;
    FieldType type_ = FieldType::Text;
};

}  // namespace mondoc::domain
