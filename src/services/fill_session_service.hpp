#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "domain/fill_session.hpp"
#include "domain/i_fill_session_repository.hpp"
#include "domain/i_template_repository.hpp"
#include "mondoc/error.hpp"
#include "mondoc/expected.hpp"
#include "mondoc/id.hpp"

namespace mondoc::services {

enum class ExportFormat { Docx, Pdf, Text, Markdown };

class FillSessionService {
public:
    FillSessionService(mondoc::domain::IFillSessionRepository& sessionRepo,
                       mondoc::domain::ITemplateRepository& templateRepo) noexcept;

    mondoc::expected<mondoc::FillSessionId, mondoc::Error>
    openSession(const mondoc::TemplateId& templateId);

    mondoc::expected<void, mondoc::Error>
    setFieldValue(const mondoc::FillSessionId& sessionId,
                  const mondoc::FieldId& fieldId,
                  const std::string& value);

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
};

}  // namespace mondoc::services
