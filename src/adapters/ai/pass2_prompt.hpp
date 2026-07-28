#pragma once
#include <string>
#include <string_view>
#include <vector>
#include "ai_fill_pipeline.hpp"
#include "domain/template.hpp"

namespace mondoc::adapters::ai {

inline constexpr std::string_view kPass2SystemPrompt =
    "You map extracted facts onto template fields. For each template field, "
    "select the best supporting fact, format the value to match the field "
    "type (text/date/number/etc.), and assign a confidence bucket of "
    "'high', 'medium', or 'low'. Every field MUST receive a value. If "
    "confidence is low, emit your best guess but mark confidence='low' — "
    "never return an empty string. Return only JSON matching the supplied "
    "schema.";

inline std::string fieldTypeToLabel(mondoc::domain::FieldType t) {
    using mondoc::domain::FieldType;
    switch (t) {
        case FieldType::Text:      return "Text";
        case FieldType::Paragraph: return "Paragraph";
        case FieldType::Number:    return "Number";
        case FieldType::Date:      return "Date";
        case FieldType::Checkbox:  return "Checkbox";
        case FieldType::Dropdown:  return "Dropdown";
    }
    return "Text";
}

// Shared with the refine prompt (XC-9): both prompts embed the same
// evidence facts using this exact format so the LLM sees identical framing
// whether it's assigning a fact to a field (Pass 2) or reconsidering a
// previously assigned value (refine).
inline std::string buildFactsSection(const std::vector<AiExtractedFact>& facts) {
    std::string out = "\nExtracted facts (with fact_index):\n";
    for (std::size_t i = 0; i < facts.size(); ++i) {
        out += "[" + std::to_string(i) + "] source_index=";
        out += std::to_string(facts[i].source_index_);
        out += " summary=\"";
        out += facts[i].summary_;
        out += "\" excerpt=\"";
        out += facts[i].excerpt_;
        out += "\"\n";
    }
    return out;
}

inline std::string buildPass2UserPrompt(const mondoc::domain::Template& tpl,
                                        const std::vector<AiExtractedFact>& facts) {
    std::string out = "Template fields:\n";
    for (const auto& f : tpl.fields_) {
        out += "- field_id=\"";
        out += f.id_.value();
        out += "\" name=\"";
        out += f.name_;
        out += "\" type=\"";
        out += fieldTypeToLabel(f.type_);
        out += "\"\n";
    }
    out += buildFactsSection(facts);
    return out;
}

inline constexpr std::string_view kPass2JsonSchema = R"JSON({
  "type": "object",
  "properties": {
    "fills": {
      "type": "array",
      "items": {
        "type": "object",
        "properties": {
          "field_id":   {"type": "string"},
          "value":      {"type": "string"},
          "confidence": {"type": "string", "enum": ["high","medium","low"]},
          "fact_index": {"type": "integer"}
        },
        "required": ["field_id","value","confidence","fact_index"],
        "additionalProperties": false
      }
    }
  },
  "required": ["fills"],
  "additionalProperties": false
})JSON";

}  // namespace mondoc::adapters::ai
