#pragma once
#include <string>
#include <string_view>
#include <vector>
#include "ai_fill_pipeline.hpp"
#include "domain/fill.hpp"
#include "domain/template.hpp"

namespace mondoc::adapters::ai {

inline constexpr std::string_view kRefineSystemPrompt =
    "You are refining a previously filled form. Given the template, the "
    "current field values, the originally extracted facts, and the user's "
    "refinement message, return updates ONLY for fields whose value or "
    "confidence changes. Keep formatting consistent with the field type. "
    "Return only JSON matching the supplied schema.";

std::string buildRefineUserPrompt(const mondoc::domain::Template& tpl,
                                  const std::vector<AiFillSourceDoc>& sources,
                                  const std::vector<mondoc::domain::Fill>& currentFills,
                                  const std::vector<ExtractedFact>& lastPass1Facts,
                                  const std::string& userMessage);

inline constexpr std::string_view kRefineJsonSchema = R"JSON({
  "type": "object",
  "properties": {
    "updates": {
      "type": "array",
      "items": {
        "type": "object",
        "properties": {
          "field_id":    {"type": "string"},
          "value":       {"type": "string"},
          "confidence":  {"type": "string", "enum": ["high","medium","low"]},
          "explanation": {"type": "string"}
        },
        "required": ["field_id","value","confidence","explanation"],
        "additionalProperties": false
      }
    }
  },
  "required": ["updates"],
  "additionalProperties": false
})JSON";

}  // namespace mondoc::adapters::ai
