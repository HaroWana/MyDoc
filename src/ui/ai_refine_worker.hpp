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
                   std::vector<mondoc::domain::AiSourceDoc> sources,
                   std::vector<mondoc::domain::AiExtractedFact> lastPass1Facts,
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
    mondoc::FillSessionId session_id_;
    std::string user_message_;
    std::vector<mondoc::domain::AiSourceDoc> sources_;
    std::vector<mondoc::domain::AiExtractedFact> last_pass1_facts_;
    std::atomic<bool> cancelled_{false};
};

}  // namespace mondoc::ui
