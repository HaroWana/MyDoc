#include "fill_session_service.hpp"

#include "mondoc/util.hpp"

#include <string>
#include <vector>

#include "ai_fill_pipeline.hpp"
#include "docx_document_writer.hpp"
#include "llm_error.hpp"
#include "odt_document_writer.hpp"
#include "pdf_document_writer.hpp"
#include "plain_text_extractor.hpp"
#include "text_document_writer.hpp"
#include "domain/confidence.hpp"

namespace mondoc::services {

namespace {

mondoc::Error llmErrorToError(const mondoc::adapters::ai::LlmError& e) {
    using K = mondoc::adapters::ai::LlmError::Kind;
    switch (e.kind()) {
        case K::Cancelled:   return mondoc::Error::cancelled(e.message());
        case K::Unreachable: return mondoc::Error::unreachable(e.message());
        case K::RateLimited: return mondoc::Error::rateLimited(e.message());
        case K::BadResponse: return mondoc::Error::badResponse(e.message());
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
    auto setVal = sessionRepo_.upsertValue(sessionId, fieldId, value);
    if (!setVal) return mondoc::unexpected(setVal.error());
    // A direct user edit always wins going forward: persist Manual so a
    // field the AI filled earlier stops being eligible for AI overwrite.
    return sessionRepo_.upsertConfidence(
        sessionId, fieldId, mondoc::domain::Confidence::Manual);
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
        // The LLM call above can take a long time, and the user may type a
        // manual value into this field while it's running. Protection against
        // that race must be atomic in the repo (check-then-act here would
        // still race), so the write and the Manual/non-empty guard happen in
        // a single SQL statement.
        auto written = sessionRepo_.upsertValueIfNotManual(
            sessionId, aiFill.field_id_, aiFill.current_value_);
        if (!written) return mondoc::unexpected(written.error());

        if (!*written) {
            auto freshSession = sessionRepo_.findById(sessionId);
            if (!freshSession) return mondoc::unexpected(freshSession.error());
            for (const auto& f : freshSession->fills_) {
                if (f.field_id_ == aiFill.field_id_) {
                    out.push_back(f);
                    break;
                }
            }
            continue;
        }

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
        const std::vector<AiExtractedFact>& /*lastPass1Facts*/,
        const std::atomic<bool>& cancelled) {
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

    auto pipeResult = aiPipeline_->refine(refineInput, &cancelled);
    if (!pipeResult) {
        return mondoc::unexpected(llmErrorToError(pipeResult.error()));
    }

    std::vector<mondoc::domain::Fill> out;
    out.reserve(pipeResult->size());
    for (const auto& upd : *pipeResult) {
        auto written = sessionRepo_.upsertValueIfNotManual(
            sessionId, upd.field_id_, upd.current_value_);
        if (!written) return mondoc::unexpected(written.error());
        if (!*written) continue;

        auto setConf = sessionRepo_.upsertConfidence(
            sessionId, upd.field_id_, upd.confidence_);
        if (!setConf) return mondoc::unexpected(setConf.error());
        out.push_back(upd);
    }
    return out;
}

std::optional<AiFailureKind> classifyAiFailure(const mondoc::Error& e) {
    switch (e.kind()) {
        case mondoc::Error::Kind::Cancelled:   return AiFailureKind::Cancelled;
        case mondoc::Error::Kind::Unreachable: return AiFailureKind::Unreachable;
        case mondoc::Error::Kind::RateLimited: return AiFailureKind::RateLimited;
        case mondoc::Error::Kind::BadResponse: return AiFailureKind::BadResponse;
        default: return std::nullopt;
    }
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
    return mondoc::adapters::formats::extractPlainText(path);
}

}  // namespace mondoc::services
