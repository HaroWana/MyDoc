#pragma once

#include <atomic>
#include <string>
#include <vector>

#include <QObject>
#include <QString>

#include "domain/fill.hpp"
#include "fill_session_service.hpp"
#include "mondoc/id.hpp"

namespace mondoc::ui {

class AiRefineWorker : public QObject {
    Q_OBJECT
public:
    AiRefineWorker(mondoc::services::FillSessionService& service,
                   mondoc::FillSessionId sessionId,
                   std::string userMessage,
                   std::vector<mondoc::services::AiFillSourceInput> sources,
                   std::vector<mondoc::services::AiExtractedFact> lastPass1Facts,
                   QObject* parent = nullptr);

    void requestCancel() noexcept;

public slots:
    void run();

signals:
    void finished(std::vector<mondoc::domain::Fill> fills);
    void failed(QString message);
    void cancelled();

private:
    mondoc::services::FillSessionService& service_;
    mondoc::FillSessionId sessionId_;
    std::string userMessage_;
    std::vector<mondoc::services::AiFillSourceInput> sources_;
    std::vector<mondoc::services::AiExtractedFact> lastPass1Facts_;
    std::atomic<bool> cancelled_{false};
};

}  // namespace mondoc::ui
