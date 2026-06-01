#include "template_service.hpp"

#include <uuid.h>
#include <zip.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <optional>
#include <random>
#include <string>
#include <utility>

#include "docx_document_reader.hpp"
#include "mondoc_bundle_reader.hpp"
#include "mondoc_bundle_writer.hpp"
#include "odt_document_reader.hpp"
#include "pdf_document_reader.hpp"
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

std::string pathToUtf8(const std::filesystem::path& p) {
    auto u8 = p.u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
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
    if (ext == ".odt") {
        mondoc::adapters::formats::OdtDocumentReader reader;
        return reader.read(path);
    }
    if (ext == ".pdf") {
        mondoc::adapters::formats::PdfDocumentReader reader;
        return reader.read(path);
    }
    return mondoc::unexpected(mondoc::Error::invalidArgument(
        std::string{"Unsupported format: "} + ext));
}

mondoc::expected<void, mondoc::Error>
TemplateService::saveTemplate(mondoc::domain::Template& t) {
    return repo_.save(t);
}

mondoc::expected<void, mondoc::Error>
TemplateService::exportTemplate(const mondoc::TemplateId& id,
                                const std::filesystem::path& dest) {
    auto tpl = repo_.findById(id);
    if (!tpl) {
        return mondoc::unexpected(tpl.error());
    }
    mondoc::adapters::formats::MondocBundleWriter writer;
    return writer.write(*tpl, dest);
}

mondoc::expected<mondoc::domain::Template, mondoc::Error>
TemplateService::importTemplate(const std::filesystem::path& src,
                                bool overwrite,
                                bool importAsCopy) {
    mondoc::adapters::formats::MondocBundleReader reader;
    auto read = reader.read(src);
    if (!read) {
        return mondoc::unexpected(read.error());
    }
    mondoc::domain::Template tpl = std::move(*read);

    auto all = repo_.listAll();
    if (!all) {
        return mondoc::unexpected(all.error());
    }

    std::optional<mondoc::TemplateId> conflictId;
    for (const auto& existing : *all) {
        if (existing.name_ == tpl.name_) {
            conflictId = existing.id_;
            break;
        }
    }

    if (conflictId && !overwrite && !importAsCopy) {
        return mondoc::unexpected(
            mondoc::Error::generic("ImportConflict:" + tpl.name_));
    }

    if (conflictId && overwrite) {
        auto del = repo_.remove(*conflictId);
        if (!del) {
            return mondoc::unexpected(del.error());
        }
    }

    if (importAsCopy) {
        const std::string base = tpl.name_;
        std::optional<std::string> chosen;
        for (int n = 0; n <= 10; ++n) {
            const std::string candidate =
                (n == 0) ? (base + " (imported)")
                         : (base + " (imported " + std::to_string(n + 1) + ")");
            bool collides = false;
            for (const auto& ex : *all) {
                if (ex.name_ == candidate) {
                    collides = true;
                    break;
                }
            }
            if (!collides) {
                chosen = candidate;
                break;
            }
        }
        if (!chosen) {
            return mondoc::unexpected(
                mondoc::Error::generic("cannot auto-rename: too many copies"));
        }
        tpl.name_ = *chosen;
    }

    if (!tpl.source_path_.empty()) {
        const std::string entryName = tpl.source_path_.filename().string();
        const std::filesystem::path dest = std::filesystem::temp_directory_path() /
            "mondoc_imported" / tpl.id_.value() / entryName;
        std::error_code ec;
        std::filesystem::create_directories(dest.parent_path(), ec);

        int zipErr = 0;
        const std::string zipPath = pathToUtf8(src);
        zip_t* za = zip_open(zipPath.c_str(), ZIP_RDONLY, &zipErr);
        if (za) {
            zip_file_t* zf = zip_fopen(za, entryName.c_str(), 0);
            if (zf) {
                std::ofstream out(dest, std::ios::binary);
                std::array<char, 4096> buf{};
                zip_int64_t nread = 0;
                while ((nread = zip_fread(zf, buf.data(), buf.size())) > 0) {
                    out.write(buf.data(), nread);
                }
                zip_fclose(zf);
                tpl.source_path_ = dest;
            }
            zip_close(za);
        }
    }

    auto saved = repo_.save(tpl);
    if (!saved) {
        return mondoc::unexpected(saved.error());
    }
    return tpl;
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
