#pragma once
#include <string>
#include <string_view>
#include <vector>
#include "ai_fill_pipeline.hpp"
#include "domain/fill.hpp"
#include "domain/template.hpp"
#include "pass2_prompt.hpp"

namespace mondoc::adapters::ai {

inline constexpr std::string_view kRefineSystemPrompt =
    "You are refining a previously filled form. The user has provided a "
    "refinement instruction (e.g., 'use ISO date format', 'make this more "
    "formal'). Update only the fields the instruction applies to. For each "
    "updated field, emit a new value and confidence ('high','medium','low'). "
    "Return only JSON matching the supplied schema. If no field needs an "
    "update, return an empty 'updates' array.";

inline std::string buildRefineUserPrompt(
        const mondoc::domain::Template& tpl,
        const std::vector<AiFillSourceDoc>& sources,
        const std::vector<mondoc::domain::Fill>& currentFills,
        const std::vector<ExtractedFact>& lastPass1Facts,
        const std::string& userMessage) {
    std::string out = "Current field values:\n";
    for (const auto& f : tpl.fields_) {
        const std::string* cur = nullptr;
        for (const auto& cf : currentFills) {
            if (cf.field_id_ == f.id_) { cur = &cf.current_value_; break; }
        }
        out += "- field_id=\"" + f.id_.value() + "\" name=\"" + f.name_ +
               "\" type=\"" + fieldTypeToLabel(f.type_) + "\" current=\"" +
               (cur ? *cur : std::string{}) + "\"\n";
    }
    out += buildFactsSection(lastPass1Facts);
    out += "\nSource documents (for reference):\n";
    for (std::size_t i = 0; i < sources.size(); ++i) {
        out += "--- Source " + std::to_string(i) + ": " +
               sources[i].title_ + " ---\n" + sources[i].text_ + "\n";
    }
    out += "\nUser refinement instruction: " + userMessage + "\n";
    return out;
}

inline constexpr std::string_view kRefineJsonSchema = R"JSON({
  "type": "object",
  "properties": {
    "updates": {
      "type": "array",
      "items": {
        "type": "object",
        "properties": {
          "field_id":   {"type": "string"},
          "value":      {"type": "string"},
          "confidence": {"type": "string", "enum": ["high","medium","low"]}
        },
        "required": ["field_id","value","confidence"],
        "additionalProperties": false
      }
    }
  },
  "required": ["updates"],
  "additionalProperties": false
})JSON";

}  // namespace mondoc::adapters::ai
