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

class AiFillWorker : public QObject {
    Q_OBJECT
public:
    AiFillWorker(mondoc::services::FillSessionService& service,
                 mondoc::FillSessionId sessionId,
                 std::vector<mondoc::domain::AiSourceDoc> sources,
                 std::string freeFormText,
                 QObject* parent = nullptr);

    void requestCancel() noexcept;

public slots:
    void run();

signals:
    void finished(std::vector<mondoc::domain::Fill> fills);
    void failed(QString message, int errorKind);
    void cancelled();

private:
    mondoc::services::FillSessionService& service_;
    mondoc::FillSessionId session_id_;
    std::vector<mondoc::domain::AiSourceDoc> sources_;
    std::string free_form_text_;
    std::atomic<bool> cancelled_{false};
};

}  // namespace mondoc::ui

Q_DECLARE_METATYPE(std::vector<mondoc::domain::Fill>)
