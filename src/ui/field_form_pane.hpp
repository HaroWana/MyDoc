#pragma once

#include <QString>
#include <QWidget>
#include <unordered_map>
#include <vector>

#include "domain/confidence.hpp"
#include "domain/fill.hpp"
#include "domain/source_ref.hpp"
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

public slots:
    void populateAi(const std::vector<mondoc::domain::Fill>& fills);

signals:
    void sourceRefRequested(mondoc::domain::SourceRef ref);

private:
    bool eventFilter(QObject* obj, QEvent* ev) override;
    void buildRow(const mondoc::domain::Field& field,
                  const QString& initialValue,
                  mondoc::domain::Confidence initialConfidence);
    void markFilled(QWidget* input, mondoc::domain::Confidence c);
    void commit(const mondoc::FieldId& fieldId,
                QWidget* input,
                const QString& fieldDisplayName,
                const QString& newValue);
    void rebuildFormHost();

    QScrollArea* scroll_;
    QWidget* form_;
    QVBoxLayout* form_layout_;
    QUndoStack* undo_stack_ = nullptr;
    mondoc::services::FillSessionService* service_ = nullptr;
    mondoc::FillSessionId session_id_;
    std::unordered_map<std::string, QString> last_committed_;
    std::unordered_map<std::string, QTimer*> text_edit_timers_;
    std::unordered_map<std::string, std::vector<mondoc::domain::SourceRef>> source_refs_by_field_;
    std::unordered_map<QObject*, mondoc::FieldId> input_to_field_id_;
    std::unordered_map<std::string, QWidget*> input_by_field_;
};

}  // namespace mondoc::ui
