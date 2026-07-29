#pragma once

#include <atomic>
#include <cstdint>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "domain/ai_input.hpp"
#include "domain/fill_session.hpp"
#include "domain/i_fill_session_repository.hpp"
#include "domain/i_template_repository.hpp"
#include "mondoc/error.hpp"
#include "mondoc/expected.hpp"
#include "mondoc/id.hpp"

namespace mondoc::adapters::ai { class AiFillPipeline; }

namespace mondoc::services {

enum class ExportFormat { Docx, Pdf, Text, Odt };

// Single owner of the ExportFormat → file-extension mapping; the export
// dialog's suffix handling and the writer selection must agree.
std::string_view exportFormatExtension(ExportFormat format) noexcept;

enum class AiFailureKind { Cancelled, Unreachable, RateLimited, BadResponse };

// Classifies an Error produced by llmErrorToError by kind(). Returns nullopt
// for errors not produced by the AI path (so UI can route only AI failures to
// the AI-specific dialog and let other errors fall through).
std::optional<AiFailureKind> classifyAiFailure(const mondoc::Error& e);

class FillSessionService {
public:
    FillSessionService(mondoc::domain::IFillSessionRepository& sessionRepo,
                       mondoc::domain::ITemplateRepository& templateRepo,
                       mondoc::adapters::ai::AiFillPipeline* aiPipeline = nullptr) noexcept;

    bool isAiConfigured() const noexcept { return ai_pipeline_ != nullptr; }

    void setAiPipeline(mondoc::adapters::ai::AiFillPipeline* pipeline) noexcept {
        ai_pipeline_ = pipeline;
    }

    mondoc::expected<mondoc::FillSessionId, mondoc::Error>
    openSession(const mondoc::TemplateId& templateId);

    mondoc::expected<void, mondoc::Error>
    setFieldValue(const mondoc::FillSessionId& sessionId,
                  const mondoc::FieldId& fieldId,
                  const std::string& value);

    mondoc::expected<std::vector<mondoc::domain::Fill>, mondoc::Error>
    aiFill(const mondoc::FillSessionId& sessionId,
           const std::vector<mondoc::domain::AiSourceDoc>& sources,
           const std::string& freeFormText,
           const std::atomic<bool>& cancelled);

    mondoc::expected<std::vector<mondoc::domain::Fill>, mondoc::Error>
    refineField(const mondoc::FillSessionId& sessionId,
                const std::string& userMessage,
                const std::vector<mondoc::domain::AiSourceDoc>& sources,
                const std::vector<mondoc::domain::AiExtractedFact>& lastPass1Facts,
                const std::atomic<bool>& cancelled);

    mondoc::expected<std::vector<mondoc::domain::FillSession>, mondoc::Error>
    listDrafts();

    mondoc::expected<mondoc::domain::FillSession, mondoc::Error>
    resumeSession(const mondoc::FillSessionId& sessionId);

    mondoc::expected<void, mondoc::Error>
    discardSession(const mondoc::FillSessionId& sessionId);

    mondoc::expected<void, mondoc::Error>
    exportSession(const mondoc::FillSessionId& sessionId,
                  ExportFormat format,
                  const std::filesystem::path& destPath);

    mondoc::expected<std::string, mondoc::Error>
    readSourceText(const std::filesystem::path& path);

private:
    mondoc::domain::IFillSessionRepository& session_repo_;
    mondoc::domain::ITemplateRepository& template_repo_;
    mondoc::adapters::ai::AiFillPipeline* ai_pipeline_ = nullptr;
};

}  // namespace mondoc::services
