#include "template_service.hpp"

#include <pugixml.hpp>
#include <uuid.h>
#include <zip.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <functional>
#include <iterator>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <utility>

#include <podofo/podofo.h>

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

constexpr std::uintmax_t kMaxSourceBytes = 50ULL * 1024 * 1024;

void collectParagraphText(const pugi::xml_node& node, std::string& para) {
    for (pugi::xml_node child : node.children()) {
        if (std::string_view{child.name()} == "w:t") {
            para += child.child_value();
        }
        collectParagraphText(child, para);
    }
}

void collectWtTextRecursive(const pugi::xml_node& node,
                            std::string& out,
                            bool& firstParagraph) {
    for (pugi::xml_node child : node.children()) {
        if (std::string_view{child.name()} == "w:p") {
            if (!firstParagraph) out += '\n';
            firstParagraph = false;
            std::string para;
            collectParagraphText(child, para);
            out += para;
        } else {
            collectWtTextRecursive(child, out, firstParagraph);
        }
    }
}

std::string extractDocxTextFromXml(const std::string& xml) {
    pugi::xml_document doc;
    auto pr = doc.load_buffer(xml.data(), xml.size());
    if (pr.status != pugi::status_ok) return {};
    std::string out;
    bool firstParagraph = true;
    collectWtTextRecursive(doc, out, firstParagraph);
    return out;
}

std::string extractOdtTextFromXml(const std::string& xml) {
    pugi::xml_document doc;
    auto pr = doc.load_buffer(xml.data(), xml.size());
    if (pr.status != pugi::status_ok) return {};
    std::string out;
    std::function<void(pugi::xml_node)> collectText;
    collectText = [&](pugi::xml_node node) {
        for (pugi::xml_node child : node.children()) {
            std::string_view n{child.name()};
            if (n == "text:p") {
                if (!out.empty()) out += '\n';
                std::string para;
                std::function<void(pugi::xml_node)> collectPara;
                collectPara = [&](pugi::xml_node pNode) {
                    for (pugi::xml_node c : pNode.children()) {
                        std::string_view cn{c.name()};
                        if (cn == "text:span") {
                            para += c.child_value();
                            collectPara(c);
                        } else if (std::string_view{c.name()}.empty()) {
                            para += c.value();
                        }
                    }
                };
                para += child.child_value();
                collectPara(child);
                out += para;
            } else {
                collectText(child);
            }
        }
    };
    collectText(doc);
    return out;
}

// Reads all bytes from a zip entry up to kMaxSourceBytes. Returns empty on error.
std::string readZipEntry(const std::filesystem::path& path, const char* entryName) {
    int errCode = 0;
    const std::string nativePath = pathToUtf8(path);
    zip_t* zf = zip_open(nativePath.c_str(), ZIP_RDONLY, &errCode);
    if (!zf) return {};

    zip_stat_t st;
    zip_stat_init(&st);
    if (zip_stat(zf, entryName, 0, &st) < 0) {
        zip_discard(zf);
        return {};
    }
    if ((st.valid & ZIP_STAT_SIZE) && st.size > kMaxSourceBytes) {
        zip_discard(zf);
        return {};
    }
    zip_file_t* entry = zip_fopen(zf, entryName, 0);
    if (!entry) {
        zip_discard(zf);
        return {};
    }
    std::string xml;
    if ((st.valid & ZIP_STAT_SIZE) && st.size <= kMaxSourceBytes) {
        xml.resize(static_cast<std::size_t>(st.size));
        zip_int64_t got = zip_fread(entry, xml.data(), st.size);
        zip_fclose(entry);
        zip_discard(zf);
        if (got < 0) return {};
        xml.resize(static_cast<std::size_t>(got));
    } else {
        constexpr std::size_t kChunkSize = 64 * 1024;
        std::array<char, kChunkSize> buf{};
        for (;;) {
            zip_int64_t got = zip_fread(entry, buf.data(), buf.size());
            if (got < 0) { zip_fclose(entry); zip_discard(zf); return {}; }
            if (got == 0) break;
            if (xml.size() + static_cast<std::size_t>(got) > kMaxSourceBytes) {
                zip_fclose(entry);
                zip_discard(zf);
                return {};
            }
            xml.append(buf.data(), static_cast<std::size_t>(got));
        }
        zip_fclose(entry);
        zip_discard(zf);
    }
    return xml;
}

std::string extractDocxPlainText(const std::filesystem::path& path) {
    const std::string xml = readZipEntry(path, "word/document.xml");
    if (xml.empty()) return {};
    return extractDocxTextFromXml(xml);
}

std::string extractOdtPlainText(const std::filesystem::path& path) {
    const std::string xml = readZipEntry(path, "content.xml");
    if (xml.empty()) return {};
    return extractOdtTextFromXml(xml);
}

std::string extractPdfPlainText(const std::filesystem::path& path) {
    try {
        PoDoFo::PdfMemDocument document;
        document.Load(pathToUtf8(path));
        std::string text;
        unsigned count = document.GetPages().GetCount();
        for (unsigned i = 0; i < count; i++) {
            auto& page = document.GetPages().GetPageAt(i);
            std::vector<PoDoFo::PdfTextEntry> entries;
            page.ExtractTextTo(entries);
            for (auto& e : entries) {
                text += e.Text;
                text += ' ';
            }
            text += '\n';
        }
        return text;
    } catch (...) {
        return {};
    }
}

std::string extractPlainFileText(const std::filesystem::path& path) {
    std::error_code ec;
    auto fileSize = std::filesystem::file_size(path, ec);
    if (ec || fileSize > kMaxSourceBytes) return {};
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    return std::string{std::istreambuf_iterator<char>{in},
                       std::istreambuf_iterator<char>{}};
}

}  // namespace

TemplateService::TemplateService(mondoc::domain::ITemplateRepository& repo) noexcept
    : repo_(repo) {}

mondoc::expected<DraftWithText, mondoc::Error>
TemplateService::extractDraft(const std::filesystem::path& path) {
    const std::string ext = lowercaseExtension(path);
    if (ext == ".docx") {
        mondoc::adapters::formats::DocxDocumentReader reader;
        auto draft = reader.read(path);
        if (!draft) return mondoc::unexpected(draft.error());
        return DraftWithText{std::move(*draft), extractDocxPlainText(path)};
    }
    if (ext == ".txt" || ext == ".md") {
        mondoc::adapters::formats::PlainTextDocumentReader reader;
        auto draft = reader.read(path);
        if (!draft) return mondoc::unexpected(draft.error());
        return DraftWithText{std::move(*draft), extractPlainFileText(path)};
    }
    if (ext == ".odt") {
        mondoc::adapters::formats::OdtDocumentReader reader;
        auto draft = reader.read(path);
        if (!draft) return mondoc::unexpected(draft.error());
        return DraftWithText{std::move(*draft), extractOdtPlainText(path)};
    }
    if (ext == ".pdf") {
        mondoc::adapters::formats::PdfDocumentReader reader;
        auto draft = reader.read(path);
        if (!draft) return mondoc::unexpected(draft.error());
        return DraftWithText{std::move(*draft), extractPdfPlainText(path)};
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

    if (overwrite && importAsCopy) {
        return mondoc::unexpected(mondoc::Error::invalidArgument(
            "overwrite and importAsCopy are mutually exclusive"));
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
        if (!za) {
            return mondoc::unexpected(mondoc::Error::generic(
                "cannot open bundle for extraction: " + entryName));
        }
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
        } else {
            zip_close(za);
            return mondoc::unexpected(mondoc::Error::generic(
                "cannot extract source document from bundle: " + entryName));
        }
        zip_close(za);
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
