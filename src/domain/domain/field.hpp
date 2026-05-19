#pragma once
#include <string>
#include "mondoc/id.hpp"

namespace mondoc::domain {

enum class FieldType {
    Text, Paragraph, Number, Date, Checkbox, Dropdown
};

enum class FieldOrigin {
    Unknown,      // default — placeholder pass in ODT writer
    FormControl,  // form:* element in ODT content.xml
    Placeholder,  // detected via {{}} / [] / <> regex scan
};

struct Field {
    FieldId id_;
    std::string name_;
    FieldType type_ = FieldType::Text;
    FieldOrigin origin_ = FieldOrigin::Unknown;
};

}  // namespace mondoc::domain
