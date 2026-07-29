#include "fill_session_service.hpp"

#include "mondoc/util.hpp"

#include <string>
#include <string_view>
#include <vector>

#include "ai_fill_pipeline.hpp"
#include "format_registry.hpp"
#include "llm_error.hpp"
#include "plain_text_extractor.hpp"

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

}  // namespace

std::string_view exportFormatExtension(ExportFormat format) noexcept {
    switch (format) {
        case ExportFormat::Docx: return ".docx";
        case ExportFormat::Pdf:  return ".pdf";
        case ExportFormat::Text: return ".txt";
        case ExportFormat::Odt:  return ".odt";
    }
    return "";
}

FillSessionService::FillSessionService(
    mondoc::domain::IFillSessionRepository& sessionRepo,
    mondoc::domain::ITemplateRepository& templateRepo,
    mondoc::adapters::ai::AiFillPipeline* aiPipeline) noexcept
    : session_repo_(sessionRepo),
      template_repo_(templateRepo),
      ai_pipeline_(aiPipeline) {}

mondoc::expected<mondoc::FillSessionId, mondoc::Error>
FillSessionService::openSession(const mondoc::TemplateId& templateId) {
    auto tpl = template_repo_.findById(templateId);
    if (!tpl) return mondoc::unexpected(tpl.error());

    mondoc::domain::FillSession s;
    s.id_              = mondoc::FillSessionId{generateUuid()};
    s.template_id_     = templateId;
    s.status_          = mondoc::domain::FillStatus::Created;
    s.created_at_unix_ = 0;
    s.updated_at_unix_ = 0;
    auto saved = session_repo_.save(s);
    if (!saved) return mondoc::unexpected(saved.error());
    return s.id_;
}

mondoc::expected<void, mondoc::Error>
FillSessionService::setFieldValue(const mondoc::FillSessionId& sessionId,
                                   const mondoc::FieldId& fieldId,
                                   const std::string& value) {
    // Single atomic repo call: value and Manual confidence must land together,
    // or a concurrent AI write can observe/land in the gap between two calls.
    return session_repo_.setValueManual(sessionId, fieldId, value);
}

mondoc::expected<std::vector<mondoc::domain::Fill>, mondoc::Error>
FillSessionService::aiFill(const mondoc::FillSessionId& sessionId,
                           const std::vector<mondoc::domain::AiSourceDoc>& sources,
                           const std::string& freeFormText,
                           const std::atomic<bool>& cancelled) {
    if (!ai_pipeline_) {
        return mondoc::unexpected(
            mondoc::Error::invalidArgument("AI not configured"));
    }
    auto sessionRes = session_repo_.findById(sessionId);
    if (!sessionRes) return mondoc::unexpected(sessionRes.error());
    auto tplRes = template_repo_.findById(sessionRes->template_id_);
    if (!tplRes) return mondoc::unexpected(tplRes.error());

    mondoc::adapters::ai::RunInput runInput;
    runInput.tpl_           = &(*tplRes);
    runInput.free_form_text_ = freeFormText;
    runInput.sources_       = sources;

    auto pipeResult = ai_pipeline_->run(runInput, cancelled);
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
        auto written = session_repo_.upsertValueIfNotManual(
            sessionId, aiFill.field_id_, aiFill.current_value_);
        if (!written) return mondoc::unexpected(written.error());

        if (!*written) {
            auto freshSession = session_repo_.findById(sessionId);
            if (!freshSession) return mondoc::unexpected(freshSession.error());
            for (const auto& f : freshSession->fills_) {
                if (f.field_id_ == aiFill.field_id_) {
                    out.push_back(f);
                    break;
                }
            }
            continue;
        }

        auto setConf = session_repo_.upsertConfidence(
            sessionId, aiFill.field_id_, aiFill.confidence_);
        if (!setConf) return mondoc::unexpected(setConf.error());
        auto setRefs = session_repo_.replaceSourceRefs(
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
        const std::vector<mondoc::domain::AiSourceDoc>& sources,
        const std::vector<mondoc::domain::AiExtractedFact>& lastPass1Facts,
        const std::atomic<bool>& cancelled) {
    if (!ai_pipeline_) {
        return mondoc::unexpected(
            mondoc::Error::invalidArgument("AI not configured"));
    }
    auto sessionRes = session_repo_.findById(sessionId);
    if (!sessionRes) return mondoc::unexpected(sessionRes.error());
    auto tplRes = template_repo_.findById(sessionRes->template_id_);
    if (!tplRes) return mondoc::unexpected(tplRes.error());

    mondoc::adapters::ai::RefineInput refineInput;
    refineInput.tpl_              = &(*tplRes);
    refineInput.sources_          = sources;
    refineInput.current_fills_    = sessionRes->fills_;
    refineInput.last_pass1_facts_ = lastPass1Facts;
    refineInput.user_message_     = userMessage;

    auto pipeResult = ai_pipeline_->refine(refineInput, &cancelled);
    if (!pipeResult) {
        return mondoc::unexpected(llmErrorToError(pipeResult.error()));
    }

    std::vector<mondoc::domain::Fill> out;
    out.reserve(pipeResult->size());
    for (const auto& upd : *pipeResult) {
        auto written = session_repo_.upsertValueIfNotManual(
            sessionId, upd.field_id_, upd.current_value_);
        if (!written) return mondoc::unexpected(written.error());
        if (!*written) continue;

        auto setConf = session_repo_.upsertConfidence(
            sessionId, upd.field_id_, upd.confidence_);
        if (!setConf) return mondoc::unexpected(setConf.error());
        // DSA-8: refine has no citation model yet, so any refs stored from a
        // prior aiFill now describe stale evidence for the OLD value. Stale
        // provenance is worse than none — clear it rather than leave it
        // pointing at the wrong text.
        auto clearRefs = session_repo_.replaceSourceRefs(sessionId, upd.field_id_, {});
        if (!clearRefs) return mondoc::unexpected(clearRefs.error());
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
    return session_repo_.listDrafts();
}

mondoc::expected<mondoc::domain::FillSession, mondoc::Error>
FillSessionService::resumeSession(const mondoc::FillSessionId& id) {
    auto s = session_repo_.findById(id);
    if (!s) return mondoc::unexpected(s.error());
    if (s->status_ == mondoc::domain::FillStatus::Created) {
        s->status_ = mondoc::domain::FillStatus::Reviewing;
        auto saved = session_repo_.save(*s);
        if (!saved) return mondoc::unexpected(saved.error());
    }
    return *s;
}

mondoc::expected<void, mondoc::Error>
FillSessionService::discardSession(const mondoc::FillSessionId& id) {
    return session_repo_.remove(id);
}

mondoc::expected<void, mondoc::Error>
FillSessionService::exportSession(const mondoc::FillSessionId& id,
                                   ExportFormat format,
                                   const std::filesystem::path& destPath) {
    auto session = session_repo_.findById(id);
    if (!session) return mondoc::unexpected(session.error());
    auto tpl = template_repo_.findById(session->template_id_);
    if (!tpl) return mondoc::unexpected(tpl.error());

    auto writer = mondoc::adapters::formats::writerForExtension(
        exportFormatExtension(format));
    if (!writer) {
        return mondoc::unexpected(
            mondoc::Error::invalidArgument("unsupported format"));
    }
    auto writeResult = writer->write(*tpl, session->fills_, destPath);
    if (!writeResult) return mondoc::unexpected(writeResult.error());

    session->status_ = mondoc::domain::FillStatus::Exported;
    auto saved = session_repo_.save(*session);
    if (!saved) return mondoc::unexpected(saved.error());
    return {};
}

mondoc::expected<std::string, mondoc::Error>
FillSessionService::readSourceText(const std::filesystem::path& path) {
    return mondoc::adapters::formats::extractPlainText(path);
}

}  // namespace mondoc::services
