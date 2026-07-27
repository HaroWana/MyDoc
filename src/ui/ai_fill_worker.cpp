#include "ai_fill_worker.hpp"

#include <utility>

#include <QtGlobal>

namespace mondoc::ui {

AiFillWorker::AiFillWorker(mondoc::services::FillSessionService& service,
                           mondoc::FillSessionId sessionId,
                           std::vector<mondoc::services::AiFillSourceInput> sources,
                           std::string freeFormText,
                           QObject* parent)
    : QObject(parent),
      service_(service),
      sessionId_(std::move(sessionId)),
      sources_(std::move(sources)),
      freeFormText_(std::move(freeFormText)) {}

void AiFillWorker::requestCancel() noexcept {
    cancelled_.store(true, std::memory_order_relaxed);
}

void AiFillWorker::run() {
    qInfo("AiFillWorker::run started");
    auto result = service_.aiFill(sessionId_, sources_, freeFormText_, cancelled_);
    if (result) {
        qInfo("AiFillWorker::run finished");
        emit finished(std::move(*result));
        return;
    }
    const auto kind = mondoc::services::classifyAiFailure(result.error());
    if (kind && *kind == mondoc::services::AiFailureKind::Cancelled) {
        qInfo("AiFillWorker::run cancelled");
        emit cancelled();
        return;
    }
    const char* kindName = "Unknown";
    if (kind) {
        switch (*kind) {
            case mondoc::services::AiFailureKind::Unreachable: kindName = "Unreachable"; break;
            case mondoc::services::AiFailureKind::RateLimited: kindName = "RateLimited"; break;
            case mondoc::services::AiFailureKind::BadResponse: kindName = "BadResponse"; break;
            case mondoc::services::AiFailureKind::Cancelled:   kindName = "Cancelled";   break;
        }
    }
    qInfo("AiFillWorker::run failed (%s)", kindName);
    emit failed(QString::fromStdString(result.error().message()),
                static_cast<int>(result.error().kind()));
}

}  // namespace mondoc::ui
