#pragma once

#include <QString>
#include <QWidget>
#include <unordered_map>

#include "domain/template.hpp"
#include "domain/fill_session.hpp"
#include "fill_session_service.hpp"
#include "mondoc/id.hpp"

class QScrollArea;
class QUndoStack;
class QVBoxLayout;
class QTimer;

namespace mondoc::ui {

class FieldFormPane : public QWidget {
    Q_OBJECT
public:
    explicit FieldFormPane(QWidget* parent = nullptr);

    void populate(const mondoc::domain::Template& tpl,
                  const mondoc::domain::FillSession& session,
                  mondoc::services::FillSessionService& service,
                  QUndoStack* undoStack,
                  mondoc::FillSessionId sessionId);

    void clear();

private:
    void buildRow(const mondoc::domain::Field& field, const QString& initialValue);
    void markFilled(QWidget* input);
    void commit(const mondoc::FieldId& fieldId,
                QWidget* input,
                const QString& fieldDisplayName,
                const QString& newValue);
    void rebuildFormHost();

    QScrollArea* scroll_;
    QWidget* form_;
    QVBoxLayout* formLayout_;
    QUndoStack* undoStack_ = nullptr;
    mondoc::services::FillSessionService* service_ = nullptr;
    mondoc::FillSessionId sessionId_;
    std::unordered_map<std::string, QString> lastCommitted_;
    std::unordered_map<std::string, QTimer*> textEditTimers_;
};

}  // namespace mondoc::ui
