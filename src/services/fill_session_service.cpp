#include "fill_session_service.hpp"

#include <pugixml.hpp>
#include <uuid.h>
#include <zip.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <ios>
#include <iterator>
#include <random>
#include <string>
#include <string_view>
#include <utility>

#include "docx_document_writer.hpp"
#include "pdf_document_writer.hpp"
#include "text_document_writer.hpp"

namespace mondoc::services {

namespace {

constexpr std::uintmax_t kMaxSourceBytes = 50ULL * 1024 * 1024;  // 50 MB

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

mondoc::expected<std::string, mondoc::Error>
extractDocxText(const std::filesystem::path& path) {
    int errCode = 0;
    const std::string nativePath = pathToUtf8(path);
    zip_t* zf = zip_open(nativePath.c_str(), ZIP_RDONLY, &errCode);
    if (!zf) {
        zip_error_t ze;
        zip_error_init_with_code(&ze, errCode);
        std::string msg = zip_error_strerror(&ze);
        zip_error_fini(&ze);
        return mondoc::unexpected(mondoc::Error::generic(std::move(msg)));
    }

    zip_stat_t st;
    zip_stat_init(&st);
    if (zip_stat(zf, "word/document.xml", 0, &st) < 0) {
        zip_discard(zf);
        return mondoc::unexpected(mondoc::Error::generic(
            "word/document.xml not found"));
    }
    if ((st.valid & ZIP_STAT_SIZE) && st.size > kMaxSourceBytes) {
        zip_discard(zf);
        return mondoc::unexpected(mondoc::Error::generic("docx too large"));
    }
    zip_file_t* entry = zip_fopen(zf, "word/document.xml", 0);
    if (!entry) {
        zip_discard(zf);
        return mondoc::unexpected(mondoc::Error::generic(
            "failed to open word/document.xml"));
    }
    std::string xml;
    if ((st.valid & ZIP_STAT_SIZE) && st.size <= kMaxSourceBytes) {
        xml.resize(static_cast<std::size_t>(st.size));
        zip_int64_t got = zip_fread(entry, xml.data(), st.size);
        zip_fclose(entry);
        zip_discard(zf);
        if (got < 0) {
            return mondoc::unexpected(mondoc::Error::generic(
                "read error in word/document.xml"));
        }
        xml.resize(static_cast<std::size_t>(got));
    } else {
        constexpr std::size_t kChunkSize = 64 * 1024;
        std::array<char, kChunkSize> buf{};
        for (;;) {
            zip_int64_t got = zip_fread(entry, buf.data(), buf.size());
            if (got < 0) {
                zip_fclose(entry);
                zip_discard(zf);
                return mondoc::unexpected(mondoc::Error::generic(
                    "read error in word/document.xml"));
            }
            if (got == 0) break;
            if (xml.size() + static_cast<std::size_t>(got) > kMaxSourceBytes) {
                zip_fclose(entry);
                zip_discard(zf);
                return mondoc::unexpected(mondoc::Error::generic("docx too large"));
            }
            xml.append(buf.data(), static_cast<std::size_t>(got));
        }
        zip_fclose(entry);
        zip_discard(zf);
    }

    pugi::xml_document doc;
    auto pr = doc.load_buffer(xml.data(), xml.size());
    if (pr.status != pugi::status_ok) {
        return mondoc::unexpected(mondoc::Error::generic(pr.description()));
    }
    std::string out;
    bool firstParagraph = true;
    collectWtTextRecursive(doc, out, firstParagraph);
    return out;
}

}  // namespace

FillSessionService::FillSessionService(
    mondoc::domain::IFillSessionRepository& sessionRepo,
    mondoc::domain::ITemplateRepository& templateRepo,
    mondoc::adapters::ai::AiFillPipeline* aiPipeline) noexcept
    : sessionRepo_(sessionRepo),
      templateRepo_(templateRepo),
      aiPipeline_(aiPipeline) {}

mondoc::expected<mondoc::FillSessionId, mondoc::Error>
FillSessionService::openSession(const mondoc::TemplateId& templateId) {
    auto tpl = templateRepo_.findById(templateId);
    if (!tpl) return mondoc::unexpected(tpl.error());

    mondoc::domain::FillSession s;
    s.id_              = mondoc::FillSessionId{generateUuid()};
    s.template_id_     = templateId;
    s.status_          = mondoc::domain::FillStatus::Created;
    s.created_at_unix_ = 0;
    s.updated_at_unix_ = 0;
    auto saved = sessionRepo_.save(s);
    if (!saved) return mondoc::unexpected(saved.error());
    return s.id_;
}

mondoc::expected<void, mondoc::Error>
FillSessionService::setFieldValue(const mondoc::FillSessionId& sessionId,
                                   const mondoc::FieldId& fieldId,
                                   const std::string& value) {
    return sessionRepo_.upsertValue(sessionId, fieldId, value);
}

mondoc::expected<std::vector<mondoc::domain::FillSession>, mondoc::Error>
FillSessionService::listDrafts() {
    return sessionRepo_.listDrafts();
}

mondoc::expected<mondoc::domain::FillSession, mondoc::Error>
FillSessionService::resumeSession(const mondoc::FillSessionId& id) {
    auto s = sessionRepo_.findById(id);
    if (!s) return mondoc::unexpected(s.error());
    if (s->status_ == mondoc::domain::FillStatus::Created) {
        s->status_ = mondoc::domain::FillStatus::Reviewing;
        auto saved = sessionRepo_.save(*s);
        if (!saved) return mondoc::unexpected(saved.error());
    }
    return *s;
}

mondoc::expected<void, mondoc::Error>
FillSessionService::discardSession(const mondoc::FillSessionId& id) {
    return sessionRepo_.remove(id);
}

mondoc::expected<void, mondoc::Error>
FillSessionService::exportSession(const mondoc::FillSessionId& id,
                                   ExportFormat format,
                                   const std::filesystem::path& destPath) {
    auto session = sessionRepo_.findById(id);
    if (!session) return mondoc::unexpected(session.error());
    auto tpl = templateRepo_.findById(session->template_id_);
    if (!tpl) return mondoc::unexpected(tpl.error());

    mondoc::expected<void, mondoc::Error> writeResult =
        mondoc::unexpected(mondoc::Error::invalidArgument("unsupported format"));
    switch (format) {
        case ExportFormat::Docx: {
            mondoc::adapters::formats::DocxDocumentWriter w;
            writeResult = w.write(*tpl, session->fills_, destPath);
            break;
        }
        case ExportFormat::Pdf: {
            mondoc::adapters::formats::PdfDocumentWriter w;
            writeResult = w.write(*tpl, session->fills_, destPath);
            break;
        }
        case ExportFormat::Text:
        case ExportFormat::Markdown: {
            mondoc::adapters::formats::TextDocumentWriter w;
            writeResult = w.write(*tpl, session->fills_, destPath);
            break;
        }
    }
    if (!writeResult) return mondoc::unexpected(writeResult.error());

    session->status_ = mondoc::domain::FillStatus::Exported;
    auto saved = sessionRepo_.save(*session);
    if (!saved) return mondoc::unexpected(saved.error());
    return {};
}

mondoc::expected<std::string, mondoc::Error>
FillSessionService::readSourceText(const std::filesystem::path& path) {
    const std::string ext = lowercaseExtension(path);
    if (ext == ".pdf") {
        return mondoc::unexpected(mondoc::Error::invalidArgument(
            "PDF source reading is Phase 4 (deferred)"));
    }
    if (ext == ".docx") {
        return extractDocxText(path);
    }
    if (ext == ".txt" || ext == ".md") {
        std::error_code ec;
        auto fileSize = std::filesystem::file_size(path, ec);
        if (ec) {
            return mondoc::unexpected(mondoc::Error::generic(
                std::string{"cannot stat file: "} + ec.message()));
        }
        if (fileSize > kMaxSourceBytes) {
            return mondoc::unexpected(mondoc::Error::generic("file too large"));
        }
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            return mondoc::unexpected(mondoc::Error::generic("failed to open file"));
        }
        return std::string{std::istreambuf_iterator<char>{in},
                           std::istreambuf_iterator<char>{}};
    }
    return mondoc::unexpected(mondoc::Error::invalidArgument(
        std::string{"unsupported source format: "} + ext));
}

}  // namespace mondoc::services
