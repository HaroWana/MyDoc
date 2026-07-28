#include "ai_field_detect_worker.hpp"

#include <utility>

#include <QtGlobal>

#include "llm_error.hpp"

namespace mondoc::ui {

AiFieldDetectWorker::AiFieldDetectWorker(
        mondoc::adapters::ai::AiFieldDetector& detector,
        std::string documentText,
        std::vector<mondoc::domain::Field> existingFields,
        QObject* parent)
    : QObject(parent),
      detector_(detector),
      document_text_(std::move(documentText)),
      existing_fields_(std::move(existingFields)) {}

void AiFieldDetectWorker::requestCancel() noexcept {
    cancelled_.store(true, std::memory_order_relaxed);
}

void AiFieldDetectWorker::run() {
    qInfo("AiFieldDetectWorker::run started");
    auto result = detector_.detect(document_text_, existing_fields_, cancelled_);
    if (result) {
        qInfo("AiFieldDetectWorker::run finished");
        emit proposalsReady(std::move(result->new_fields), std::move(result->improvements));
        return;
    }
    const auto kind = result.error().kind();
    if (kind == mondoc::adapters::ai::LlmError::Kind::Cancelled) {
        qInfo("AiFieldDetectWorker::run cancelled");
        emit cancelled();
        return;
    }
    qWarning("AiFieldDetectWorker::run failed");
    emit failed(QString::fromStdString(result.error().message()),
                static_cast<int>(kind));
}

}  // namespace mondoc::ui
