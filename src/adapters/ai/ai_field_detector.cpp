#include "ai_field_detector.hpp"

namespace mondoc::adapters::ai {

AiFieldDetector::AiFieldDetector(ILlmClient& client, LlmConfig config) noexcept
    : client_(client), config_(std::move(config)) {}

mondoc::expected<DetectionResult, LlmError>
AiFieldDetector::detect(const std::string& /*documentText*/,
                        const std::vector<mondoc::domain::Field>& /*existingFields*/,
                        const std::atomic<bool>& /*cancelled*/) {
    return mondoc::unexpected<LlmError>(LlmError::badResponse("not implemented"));
}

}  // namespace mondoc::adapters::ai
