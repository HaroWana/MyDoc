#include "ai_refine_worker.hpp"

#include <utility>

#include <QtGlobal>

namespace mondoc::ui {

AiRefineWorker::AiRefineWorker(mondoc::services::FillSessionService& service,
                               mondoc::FillSessionId sessionId,
                               std::string userMessage,
                               std::vector<mondoc::domain::AiSourceDoc> sources,
                               std::vector<mondoc::domain::AiExtractedFact> lastPass1Facts,
                               QObject* parent)
    : QObject(parent),
      service_(service),
      sessionId_(std::move(sessionId)),
      userMessage_(std::move(userMessage)),
      sources_(std::move(sources)),
      lastPass1Facts_(std::move(lastPass1Facts)) {}

void AiRefineWorker::requestCancel() noexcept {
    cancelled_.store(true, std::memory_order_relaxed);
}

void AiRefineWorker::run() {
    qInfo("AiRefineWorker::run started");
    auto result = service_.refineField(sessionId_, userMessage_, sources_,
                                        lastPass1Facts_, cancelled_);
    if (result) {
        qInfo("AiRefineWorker::run finished");
        emit finished(std::move(*result));
        return;
    }
    const auto kind = mondoc::services::classifyAiFailure(result.error());
    if (kind && *kind == mondoc::services::AiFailureKind::Cancelled) {
        qInfo("AiRefineWorker::run cancelled");
        emit cancelled();
        return;
    }
    qWarning("AiRefineWorker::run failed");
    emit failed(QString::fromStdString(result.error().message()));
}

}  // namespace mondoc::ui
