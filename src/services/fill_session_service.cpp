#include "fill_session_service.hpp"

namespace mondoc::services {

FillSessionService::FillSessionService(
    mondoc::domain::IFillSessionRepository& sessionRepo,
    mondoc::domain::ITemplateRepository& templateRepo) noexcept
    : sessionRepo_(sessionRepo), templateRepo_(templateRepo) {}

mondoc::expected<mondoc::FillSessionId, mondoc::Error>
FillSessionService::openSession(const mondoc::TemplateId&) {
    return mondoc::unexpected(mondoc::Error::generic("not implemented"));
}

mondoc::expected<void, mondoc::Error>
FillSessionService::setFieldValue(const mondoc::FillSessionId&,
                                   const mondoc::FieldId&,
                                   const std::string&) {
    return mondoc::unexpected(mondoc::Error::generic("not implemented"));
}

mondoc::expected<std::vector<mondoc::domain::FillSession>, mondoc::Error>
FillSessionService::listDrafts() {
    return mondoc::unexpected(mondoc::Error::generic("not implemented"));
}

mondoc::expected<mondoc::domain::FillSession, mondoc::Error>
FillSessionService::resumeSession(const mondoc::FillSessionId&) {
    return mondoc::unexpected(mondoc::Error::generic("not implemented"));
}

mondoc::expected<void, mondoc::Error>
FillSessionService::discardSession(const mondoc::FillSessionId&) {
    return mondoc::unexpected(mondoc::Error::generic("not implemented"));
}

mondoc::expected<void, mondoc::Error>
FillSessionService::exportSession(const mondoc::FillSessionId&,
                                   ExportFormat,
                                   const std::filesystem::path&) {
    return mondoc::unexpected(mondoc::Error::generic("not implemented"));
}

mondoc::expected<std::string, mondoc::Error>
FillSessionService::readSourceText(const std::filesystem::path&) {
    return mondoc::unexpected(mondoc::Error::generic("not implemented"));
}

}  // namespace mondoc::services
