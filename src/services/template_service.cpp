#include "template_service.hpp"

#include "mondoc/util.hpp"

#include <zip.h>

#include <cstdint>
#include <fstream>
#include <optional>
#include <string>
#include <utility>

#include "detail/zip_util.hpp"
#include "format_registry.hpp"
#include "mondoc_bundle_reader.hpp"
#include "mondoc_bundle_writer.hpp"
#include "plain_text_extractor.hpp"

namespace mondoc::services {

namespace {
constexpr std::uint64_t kMaxImportedSourceBytes = 50ULL * 1024 * 1024;
}  // namespace

TemplateService::TemplateService(mondoc::domain::ITemplateRepository& repo,
                                 std::filesystem::path dataDir)
    : repo_(repo), dataDir_(std::move(dataDir)) {}

mondoc::expected<DraftWithText, mondoc::Error>
TemplateService::extractDraft(const std::filesystem::path& path) {
    auto reader = mondoc::adapters::formats::readerForPath(path);
    if (!reader) {
        return mondoc::unexpected(mondoc::Error::invalidArgument(
            std::string{"Unsupported format: "} + lowercaseExtension(path)));
    }
    auto draft = reader->read(path);
    if (!draft) return mondoc::unexpected(draft.error());

    // Once the schema is in hand the template is registrable and manually
    // fillable, which the app must remain fully usable for without AI.
    // document_text only feeds AI field detection, so a text-extraction
    // failure degrades to empty text rather than failing registration —
    // PoDoFo in particular can load a valid AcroForm yet throw while
    // extracting text.
    auto text = mondoc::adapters::formats::extractPlainText(path);
    return DraftWithText{std::move(*draft),
                         text ? std::move(*text) : std::string{}};
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

    if (overwrite && importAsCopy) {
        return mondoc::unexpected(mondoc::Error::invalidArgument(
            "overwrite and importAsCopy are mutually exclusive"));
    }

    if (conflictId && !overwrite && !importAsCopy) {
        return mondoc::unexpected(mondoc::Error::conflict(tpl.name_));
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
        tpl.id_ = mondoc::TemplateId{generateUuid()};
        for (auto& f : tpl.fields_) {
            f.id_ = mondoc::FieldId{generateUuid()};
        }
    }

    if (!tpl.source_path_.empty()) {
        const std::string entryName = tpl.source_path_.filename().string();
        const std::filesystem::path destDir = dataDir_ / "imported" / tpl.id_.value();
        const std::filesystem::path dest = destDir / entryName;

        std::error_code ec;
        std::filesystem::create_directories(destDir, ec);
        if (ec) {
            return mondoc::unexpected(mondoc::Error::generic(
                "cannot create import directory: " + ec.message()));
        }

        int zipErr = 0;
        const std::string zipPath = pathToUtf8(src);
        zip_t* za = zip_open(zipPath.c_str(), ZIP_RDONLY, &zipErr);
        if (!za) {
            return mondoc::unexpected(mondoc::Error::generic(
                "cannot open bundle for extraction: " + entryName));
        }

        auto entryData = mondoc::adapters::formats::detail::readZipEntry(
            za, entryName.c_str(), kMaxImportedSourceBytes);
        zip_close(za);
        if (!entryData) {
            return mondoc::unexpected(entryData.error());
        }

        std::ofstream out(dest, std::ios::binary);
        if (out) {
            out.write(entryData->data(),
                      static_cast<std::streamsize>(entryData->size()));
            out.close();
        }
        if (!out) {
            std::error_code rmEc;
            std::filesystem::remove(dest, rmEc);
            return mondoc::unexpected(mondoc::Error::generic(
                "cannot write extracted source document: " + entryName));
        }

        tpl.source_path_ = dest;
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

    auto all = repo_.listAll();
    if (!all) {
        return mondoc::unexpected(all.error());
    }
    for (const auto& existing : *all) {
        if (existing.id_.value() != id.value() && existing.name_ == newName) {
            return mondoc::unexpected(mondoc::Error::conflict(newName));
        }
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

    auto all = repo_.listAll();
    if (!all) {
        return mondoc::unexpected(all.error());
    }
    for (const auto& existing : *all) {
        if (existing.name_ == newName) {
            return mondoc::unexpected(mondoc::Error::conflict(newName));
        }
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
