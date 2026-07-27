#pragma once
#include <string>
#include <string_view>
#include <vector>
#include "domain/field.hpp"

namespace mondoc::adapters::ai {

inline constexpr std::string_view kDetectSystemPrompt =
    "You are a document field extractor. Given a document's text and a list "
    "of already-detected fields, you: (1) propose new fillable fields not yet "
    "detected, and (2) suggest better names or types for existing fields where "
    "the current name or type is clearly wrong or imprecise. "
    "Return only JSON matching the supplied schema. Do not explain your output. "
    "Do not propose fields from fixed legal boilerplate, standard clauses, or "
    "repeated header/footer text — only propose fields for genuine fillable "
    "placeholders or labeled blanks. "
    "Do not suggest renames that only change capitalization or underscore style "
    "without improving semantics. "
    "Include low-salience fields such as checkboxes, reference numbers, and "
    "version indicators when they are genuine fillable fields in the document.";

inline std::string fieldTypeToLowercase(mondoc::domain::FieldType t) {
    using mondoc::domain::FieldType;
    switch (t) {
        case FieldType::Paragraph: return "paragraph";
        case FieldType::Number:    return "number";
        case FieldType::Date:      return "date";
        case FieldType::Checkbox:  return "checkbox";
        case FieldType::Dropdown:  return "dropdown";
        default:                   return "text";
    }
}

inline std::string buildDetectUserPrompt(const std::string& docText,
                                          const std::vector<mondoc::domain::Field>& existing) {
    std::string out = "Document text:\n";
    out += docText;
    out += "\n\nAlready-detected fields (do not propose these again):\n";
    for (const auto& f : existing) {
        out += "- ";
        out += f.name_;
        out += " (";
        out += fieldTypeToLowercase(f.type_);
        out += ")\n";
    }
    return out;
}

inline constexpr std::string_view kDetectJsonSchema = R"JSON({
  "type": "object",
  "properties": {
    "new_fields": {
      "type": "array",
      "items": {
        "type": "object",
        "properties": {
          "name": {"type": "string"},
          "type": {"type": "string", "enum": ["text","paragraph","number","date","checkbox","dropdown"]}
        },
        "required": ["name", "type"],
        "additionalProperties": false
      }
    },
    "improvements": {
      "type": "array",
      "items": {
        "type": "object",
        "properties": {
          "field_name":     {"type": "string"},
          "suggested_name": {"type": "string"},
          "suggested_type": {"type": "string", "enum": ["text","paragraph","number","date","checkbox","dropdown"]}
        },
        "required": ["field_name", "suggested_name", "suggested_type"],
        "additionalProperties": false
      }
    }
  },
  "required": ["new_fields", "improvements"],
  "additionalProperties": false
})JSON";

}  // namespace mondoc::adapters::ai
