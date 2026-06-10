#pragma once
#include <atomic>
#include <string>
#include <vector>
#include "domain/field.hpp"
#include "i_llm_client.hpp"
#include "llm_config.hpp"
#include "llm_error.hpp"
#include "mondoc/expected.hpp"

namespace mondoc::adapters::ai {

struct FieldImprovement {
    std::string field_name;
    std::string suggested_name;
    std::string suggested_type; // text|paragraph|number|date|checkbox|dropdown
};

struct DetectionResult {
    std::vector<mondoc::domain::Field> new_fields;
    std::vector<FieldImprovement> improvements;
};

class AiFieldDetector {
public:
    AiFieldDetector(ILlmClient& client, LlmConfig config) noexcept;

    mondoc::expected<DetectionResult, LlmError>
    detect(const std::string& documentText,
           const std::vector<mondoc::domain::Field>& existingFields,
           const std::atomic<bool>& cancelled);

private:
    ILlmClient& client_;
    LlmConfig config_;
};

}  // namespace mondoc::adapters::ai
