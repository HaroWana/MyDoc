#pragma once
#include <optional>
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
    Ai,           // Phase 6: proposed by LLM, pending accept/discard
};

struct PdfLocation {
    int page_index;
    double x, y, w, h;
};

struct TextLocation {
    int paragraph_index;
    int char_offset;
};

struct FieldLocation {
    std::optional<PdfLocation> pdf;
    std::optional<TextLocation> text;
};

struct Field {
    FieldId id_;
    std::string name_;
    FieldType type_ = FieldType::Text;
    FieldOrigin origin_ = FieldOrigin::Unknown;
    std::optional<FieldLocation> location_;
};

}  // namespace mondoc::domain
