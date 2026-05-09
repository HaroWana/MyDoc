#pragma once

#include <QFrame>
#include <QString>
#include <vector>

#include "mondoc/id.hpp"

class QVBoxLayout;

namespace mondoc::ui {

struct DraftSummary {
    mondoc::FillSessionId sessionId;
    QString templateName;
    QString relativeTimestamp;
};

class ResumeBanner : public QFrame {
    Q_OBJECT
public:
    explicit ResumeBanner(QWidget* parent = nullptr);

    void setDrafts(const std::vector<DraftSummary>& drafts);

signals:
    void resumeRequested(mondoc::FillSessionId sessionId);
    void discardRequested(mondoc::FillSessionId sessionId);

private:
    void clearRows();
    void confirmAndEmitDiscard(const mondoc::FillSessionId& sessionId);

    QVBoxLayout* layout_;
};

}  // namespace mondoc::ui
