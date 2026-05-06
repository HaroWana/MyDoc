#include "template_service.hpp"

#include <uuid.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <random>
#include <string>
#include <utility>

#include "docx_document_reader.hpp"
#include "plain_text_document_reader.hpp"

namespace mondoc::services {

namespace {

std::string generateUuid() {
    static thread_local std::mt19937 generator{[] {
        std::random_device rd;
        std::array<std::seed_seq::result_type, std::mt19937::state_size> seed{};
        std::generate(seed.begin(), seed.end(), std::ref(rd));
        std::seed_seq seq(seed.begin(), seed.end());
        return std::mt19937{seq};
    }()};
    uuids::uuid_random_generator gen{generator};
    return uuids::to_string(gen());
}

std::string lowercaseExtension(const std::filesystem::path& path) {
    auto ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return ext;
}

}  // namespace

TemplateService::TemplateService(mondoc::domain::ITemplateRepository& repo) noexcept
    : repo_(repo) {}

mondoc::expected<mondoc::domain::Template, mondoc::Error>
TemplateService::extractDraft(const std::filesystem::path& path) {
    const std::string ext = lowercaseExtension(path);
    if (ext == ".docx") {
        mondoc::adapters::formats::DocxDocumentReader reader;
        return reader.read(path);
    }
    if (ext == ".txt" || ext == ".md") {
        mondoc::adapters::formats::PlainTextDocumentReader reader;
        return reader.read(path);
    }
    return mondoc::unexpected(mondoc::Error::invalidArgument(
        std::string{"Unsupported format: "} + ext));
}

mondoc::expected<void, mondoc::Error>
TemplateService::saveTemplate(mondoc::domain::Template& t) {
    return repo_.save(t);
}

mondoc::expected<std::vector<mondoc::domain::Template>, mondoc::Error>
TemplateService::listTemplates() {
    return repo_.listAll();
}

mondoc::expected<void, mondoc::Error>
TemplateService::deleteTemplate(const mondoc::TemplateId& id) {
    return repo_.remove(id);
}

mondoc::expected<void, mondoc::Error>
TemplateService::renameTemplate(const mondoc::TemplateId& id,
                                const std::string& newName) {
    auto found = repo_.findById(id);
    if (!found) {
        return mondoc::unexpected(found.error());
    }
    found->name_ = newName;
    return repo_.save(*found);
}

mondoc::expected<mondoc::TemplateId, mondoc::Error>
TemplateService::duplicateTemplate(const mondoc::TemplateId& id,
                                   const std::string& newName) {
    auto found = repo_.findById(id);
    if (!found) {
        return mondoc::unexpected(found.error());
    }
    mondoc::domain::Template copy = *found;
    copy.id_   = mondoc::TemplateId{generateUuid()};
    copy.name_ = newName;
    for (auto& f : copy.fields_) {
        f.id_ = mondoc::FieldId{generateUuid()};
    }
    auto saved = repo_.save(copy);
    if (!saved) {
        return mondoc::unexpected(saved.error());
    }
    return copy.id_;
}

}  // namespace mondoc::services
