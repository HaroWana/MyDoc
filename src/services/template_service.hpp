#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "domain/i_template_repository.hpp"
#include "domain/template.hpp"
#include "mondoc/error.hpp"
#include "mondoc/expected.hpp"
#include "mondoc/id.hpp"

namespace mondoc::services {

// Returned by importTemplate when the bundle's template name collides with an
// existing template. The UI checks for this and shows ImportConflictDialog.
struct ImportConflictInfo {
    std::string conflicting_name;
};

class TemplateService {
public:
    explicit TemplateService(mondoc::domain::ITemplateRepository& repo) noexcept;

    mondoc::expected<mondoc::domain::Template, mondoc::Error>
    extractDraft(const std::filesystem::path& path);

    mondoc::expected<void, mondoc::Error>
    exportTemplate(const mondoc::TemplateId& id, const std::filesystem::path& dest);

    // On name collision returns Error with message prefixed "ImportConflict:".
    // Call again with overwrite=true to replace, or importAsCopy=true to rename.
    mondoc::expected<mondoc::domain::Template, mondoc::Error>
    importTemplate(const std::filesystem::path& src,
                   bool overwrite = false,
                   bool importAsCopy = false);

    mondoc::expected<void, mondoc::Error>
    saveTemplate(mondoc::domain::Template& t);

    mondoc::expected<std::vector<mondoc::domain::Template>, mondoc::Error>
    listTemplates();

    mondoc::expected<void, mondoc::Error>
    deleteTemplate(const mondoc::TemplateId& id);

    mondoc::expected<void, mondoc::Error>
    renameTemplate(const mondoc::TemplateId& id, const std::string& newName);

    mondoc::expected<mondoc::TemplateId, mondoc::Error>
    duplicateTemplate(const mondoc::TemplateId& id, const std::string& newName);

private:
    mondoc::domain::ITemplateRepository& repo_;
};

}  // namespace mondoc::services
