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

std::string buildPass2UserPrompt(const mondoc::domain::Template& tpl,
                                 const std::vector<ExtractedFact>& facts);

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
