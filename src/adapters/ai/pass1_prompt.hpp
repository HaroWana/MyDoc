#pragma once
#include <string>
#include <string_view>
#include <vector>
#include "ai_fill_pipeline.hpp"

namespace mondoc::adapters::ai {

inline constexpr std::string_view kPass1SystemPrompt =
    "You extract facts from source documents. For each fact, return the "
    "source document index, character-level start and end offsets (UTF-8 "
    "byte positions within the source text), the verbatim excerpt, and a "
    "short summary. Do NOT paraphrase the excerpt — copy it byte-for-byte. "
    "Return only JSON matching the supplied schema.";

inline std::string buildPass1UserPrompt(const std::vector<AiFillSourceDoc>& sources,
                                        const std::string& freeFormText) {
    std::string out = "Source documents:\n";
    for (std::size_t i = 0; i < sources.size(); ++i) {
        out += "\n--- Source ";
        out += std::to_string(i);
        out += ": ";
        out += sources[i].title_;
        out += " ---\n";
        out += sources[i].text_;
        out += "\n";
    }
    if (!freeFormText.empty()) {
        out += "\nAdditional free-form text from user:\n";
        out += freeFormText;
        out += "\n";
    }
    return out;
}

inline constexpr std::string_view kPass1JsonSchema = R"JSON({
  "type": "object",
  "properties": {
    "facts": {
      "type": "array",
      "items": {
        "type": "object",
        "properties": {
          "source_index": {"type": "integer"},
          "char_start":   {"type": "integer"},
          "char_end":     {"type": "integer"},
          "excerpt":      {"type": "string"},
          "summary":      {"type": "string"}
        },
        "required": ["source_index","char_start","char_end","excerpt","summary"],
        "additionalProperties": false
      }
    }
  },
  "required": ["facts"],
  "additionalProperties": false
})JSON";

}  // namespace mondoc::adapters::ai
