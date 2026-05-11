#pragma once

#include <atomic>
#include <cstdint>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "domain/fill_session.hpp"
#include "domain/i_fill_session_repository.hpp"
#include "domain/i_template_repository.hpp"
#include "mondoc/error.hpp"
#include "mondoc/expected.hpp"
#include "mondoc/id.hpp"

namespace mondoc::adapters::ai { class AiFillPipeline; }

namespace mondoc::services {

enum class ExportFormat { Docx, Pdf, Text, Markdown };

struct AiFillSourceInput {
    mondoc::SourceDocId id_;
    std::string title_;
    std::string text_;
};

struct AiExtractedFact {
    std::size_t source_index_ = 0;
    std::int64_t char_start_  = 0;
    std::int64_t char_end_    = 0;
    std::string excerpt_;
    std::string summary_;
};

enum class AiFailureKind { Cancelled, Unreachable, RateLimited, BadResponse };

// Inverse of llmErrorToError's message-prefix protocol. Returns nullopt for
// errors not produced by the AI path (so UI can route only AI failures to
// the AI-specific dialog and let other errors fall through).
std::optional<AiFailureKind> classifyAiFailure(const mondoc::Error& e);

class FillSessionService {
public:
    FillSessionService(mondoc::domain::IFillSessionRepository& sessionRepo,
                       mondoc::domain::ITemplateRepository& templateRepo,
                       mondoc::adapters::ai::AiFillPipeline* aiPipeline = nullptr) noexcept;

    mondoc::expected<mondoc::FillSessionId, mondoc::Error>
    openSession(const mondoc::TemplateId& templateId);

    mondoc::expected<void, mondoc::Error>
    setFieldValue(const mondoc::FillSessionId& sessionId,
                  const mondoc::FieldId& fieldId,
                  const std::string& value);

    mondoc::expected<std::vector<mondoc::domain::Fill>, mondoc::Error>
    aiFill(const mondoc::FillSessionId& sessionId,
           const std::vector<AiFillSourceInput>& sources,
           const std::string& freeFormText,
           const std::atomic<bool>& cancelled);

    mondoc::expected<std::vector<mondoc::domain::Fill>, mondoc::Error>
    refineField(const mondoc::FillSessionId& sessionId,
                const std::string& userMessage,
                const std::vector<AiFillSourceInput>& sources,
                const std::vector<AiExtractedFact>& lastPass1Facts);

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
    mondoc::domain::IFillSessionRepository& sessionRepo_;
    mondoc::domain::ITemplateRepository& templateRepo_;
    mondoc::adapters::ai::AiFillPipeline* aiPipeline_ = nullptr;
};

}  // namespace mondoc::services
