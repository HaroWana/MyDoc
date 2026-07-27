#include "fill_session_service.hpp"

#include "mondoc/util.hpp"

#include <pugixml.hpp>
#include <zip.h>

#include <array>
#include <cstdint>
#include <functional>
#include <fstream>
#include <ios>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>

#include "ai_fill_pipeline.hpp"
#include "docx_document_writer.hpp"
#include "llm_error.hpp"
#include "odt_document_writer.hpp"
#include "pdf_document_writer.hpp"
#include "text_document_writer.hpp"
#include "domain/confidence.hpp"

#include <podofo/podofo.h>

namespace mondoc::services {

namespace {

constexpr std::uintmax_t kMaxSourceBytes = 50ULL * 1024 * 1024;  // 50 MB

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

mondoc::Error llmErrorToError(const mondoc::adapters::ai::LlmError& e) {
    using K = mondoc::adapters::ai::LlmError::Kind;
    switch (e.kind()) {
        case K::Cancelled:   return mondoc::Error::generic("ai cancelled: " + e.message());
        case K::Unreachable: return mondoc::Error::generic("ai unreachable: " + e.message());
        case K::RateLimited: return mondoc::Error::generic("ai rate-limited: " + e.message());
        case K::BadResponse: return mondoc::Error::generic("ai bad response: " + e.message());
    }
    return mondoc::Error::generic("ai unknown error");
}

std::vector<mondoc::adapters::ai::AiFillSourceDoc>
translateSources(const std::vector<mondoc::services::AiFillSourceInput>& in) {
    std::vector<mondoc::adapters::ai::AiFillSourceDoc> out;
    out.reserve(in.size());
    for (const auto& s : in) {
        out.push_back({s.id_, s.title_, s.text_});
    }
    return out;
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

mondoc::expected<std::string, mondoc::Error>
extractOdtText(const std::filesystem::path& path) {
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
    if (zip_stat(zf, "content.xml", 0, &st) < 0) {
        zip_discard(zf);
        return mondoc::unexpected(mondoc::Error::generic("content.xml not found in ODT"));
    }
    if ((st.valid & ZIP_STAT_SIZE) && st.size > kMaxSourceBytes) {
        zip_discard(zf);
        return mondoc::unexpected(mondoc::Error::generic("ODT too large"));
    }
    zip_file_t* entry = zip_fopen(zf, "content.xml", 0);
    if (!entry) {
        zip_discard(zf);
        return mondoc::unexpected(mondoc::Error::generic("failed to open content.xml"));
    }
    std::string xml;
    if ((st.valid & ZIP_STAT_SIZE) && st.size <= kMaxSourceBytes) {
        xml.resize(static_cast<std::size_t>(st.size));
        zip_int64_t got = zip_fread(entry, xml.data(), st.size);
        zip_fclose(entry);
        zip_discard(zf);
        if (got < 0) {
            return mondoc::unexpected(mondoc::Error::generic("read error in content.xml"));
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
                return mondoc::unexpected(mondoc::Error::generic("read error in content.xml"));
            }
            if (got == 0) break;
            if (xml.size() + static_cast<std::size_t>(got) > kMaxSourceBytes) {
                zip_fclose(entry);
                zip_discard(zf);
                return mondoc::unexpected(mondoc::Error::generic("ODT too large"));
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

mondoc::expected<std::string, mondoc::Error>
extractPdfText(const std::filesystem::path& path) {
    try {
        PoDoFo::PdfMemDocument document;
        document.Load(pathToUtf8(path));
        std::string text;
        unsigned count = document.GetPages().GetCount();
        for (unsigned i = 0; i < count; i++) {
            auto& page = document.GetPages().GetPageAt(i);
            std::vector<PoDoFo::PdfTextEntry> entries;
            page.ExtractTextTo(entries);
            for (auto& entry : entries) {
                text += entry.Text;
                text += ' ';
            }
            text += '\n';
        }
        return text;
    } catch (const PoDoFo::PdfError& e) {
        return mondoc::unexpected(mondoc::Error::generic(
            std::string{"podofo: "} + e.what()));
    } catch (const std::exception& e) {
        return mondoc::unexpected(mondoc::Error::generic(
            std::string{"podofo: "} + e.what()));
    }
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

mondoc::expected<std::vector<mondoc::domain::Fill>, mondoc::Error>
FillSessionService::aiFill(const mondoc::FillSessionId& sessionId,
                           const std::vector<AiFillSourceInput>& sources,
                           const std::string& freeFormText,
                           const std::atomic<bool>& cancelled) {
    if (!aiPipeline_) {
        return mondoc::unexpected(
            mondoc::Error::invalidArgument("AI not configured"));
    }
    auto sessionRes = sessionRepo_.findById(sessionId);
    if (!sessionRes) return mondoc::unexpected(sessionRes.error());
    auto tplRes = templateRepo_.findById(sessionRes->template_id_);
    if (!tplRes) return mondoc::unexpected(tplRes.error());

    mondoc::adapters::ai::RunInput runInput;
    runInput.tpl_           = &(*tplRes);
    runInput.free_form_text_ = freeFormText;
    runInput.sources_       = translateSources(sources);

    auto pipeResult = aiPipeline_->run(runInput, cancelled);
    if (!pipeResult) {
        return mondoc::unexpected(llmErrorToError(pipeResult.error()));
    }

    std::vector<mondoc::domain::Fill> out;
    out.reserve(pipeResult->size());
    for (const auto& aiFill : *pipeResult) {
        const mondoc::domain::Fill* existing = nullptr;
        for (const auto& f : sessionRes->fills_) {
            if (f.field_id_ == aiFill.field_id_) {
                existing = &f;
                break;
            }
        }
        if (existing &&
            existing->confidence_ == mondoc::domain::Confidence::Manual &&
            !existing->current_value_.empty()) {
            out.push_back(*existing);
            continue;
        }
        auto setVal = sessionRepo_.upsertValue(
            sessionId, aiFill.field_id_, aiFill.current_value_);
        if (!setVal) return mondoc::unexpected(setVal.error());
        auto setConf = sessionRepo_.upsertConfidence(
            sessionId, aiFill.field_id_, aiFill.confidence_);
        if (!setConf) return mondoc::unexpected(setConf.error());
        auto setRefs = sessionRepo_.replaceSourceRefs(
            sessionId, aiFill.field_id_, aiFill.source_refs_);
        if (!setRefs) return mondoc::unexpected(setRefs.error());
        out.push_back(aiFill);
    }
    return out;
}

mondoc::expected<std::vector<mondoc::domain::Fill>, mondoc::Error>
FillSessionService::refineField(
        const mondoc::FillSessionId& sessionId,
        const std::string& userMessage,
        const std::vector<AiFillSourceInput>& sources,
        const std::vector<AiExtractedFact>& /*lastPass1Facts*/) {
    if (!aiPipeline_) {
        return mondoc::unexpected(
            mondoc::Error::invalidArgument("AI not configured"));
    }
    auto sessionRes = sessionRepo_.findById(sessionId);
    if (!sessionRes) return mondoc::unexpected(sessionRes.error());
    auto tplRes = templateRepo_.findById(sessionRes->template_id_);
    if (!tplRes) return mondoc::unexpected(tplRes.error());

    mondoc::adapters::ai::RefineInput refineInput;
    refineInput.tpl_           = &(*tplRes);
    refineInput.sources_       = translateSources(sources);
    refineInput.current_fills_ = sessionRes->fills_;
    refineInput.user_message_  = userMessage;

    auto pipeResult = aiPipeline_->refine(refineInput);
    if (!pipeResult) {
        return mondoc::unexpected(llmErrorToError(pipeResult.error()));
    }

    std::vector<mondoc::domain::Fill> out;
    out.reserve(pipeResult->size());
    for (const auto& upd : *pipeResult) {
        const mondoc::domain::Fill* existing = nullptr;
        for (const auto& f : sessionRes->fills_) {
            if (f.field_id_ == upd.field_id_) {
                existing = &f;
                break;
            }
        }
        if (existing &&
            existing->confidence_ == mondoc::domain::Confidence::Manual &&
            !existing->current_value_.empty()) {
            continue;
        }
        auto setVal = sessionRepo_.upsertValue(
            sessionId, upd.field_id_, upd.current_value_);
        if (!setVal) return mondoc::unexpected(setVal.error());
        auto setConf = sessionRepo_.upsertConfidence(
            sessionId, upd.field_id_, upd.confidence_);
        if (!setConf) return mondoc::unexpected(setConf.error());
        out.push_back(upd);
    }
    return out;
}

std::optional<AiFailureKind> classifyAiFailure(const mondoc::Error& e) {
    const auto& m = e.message();
    if (m.rfind("ai cancelled", 0) == 0)    return AiFailureKind::Cancelled;
    if (m.rfind("ai unreachable", 0) == 0)  return AiFailureKind::Unreachable;
    if (m.rfind("ai rate-limited", 0) == 0) return AiFailureKind::RateLimited;
    if (m.rfind("ai bad response", 0) == 0) return AiFailureKind::BadResponse;
    return std::nullopt;
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
        case ExportFormat::Odt: {
            mondoc::adapters::formats::OdtDocumentWriter w;
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
    if (ext == ".odt") {
        return extractOdtText(path);
    }
    if (ext == ".pdf") {
        return extractPdfText(path);
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
