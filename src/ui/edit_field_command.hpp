#pragma once

#include <QString>
#include <QUndoCommand>

#include <string>

#include "fill_session_service.hpp"
#include "mondoc/id.hpp"

class QWidget;

namespace mondoc::ui {

class EditFieldCommand : public QUndoCommand {
public:
    EditFieldCommand(mondoc::FieldId fieldId,
                     std::string oldValue,
                     std::string newValue,
                     QWidget* inputWidget,
                     mondoc::services::FillSessionService& service,
                     mondoc::FillSessionId sessionId,
                     QString fieldDisplayName,
                     QUndoCommand* parent = nullptr);

    void redo() override;
    void undo() override;
    int id() const override { return 1; }
    bool mergeWith(const QUndoCommand* other) override;

    const mondoc::FieldId& fieldId() const noexcept { return field_id_; }

private:
    void applyValue(const std::string& value);

    mondoc::FieldId field_id_;
    std::string old_value_;
    std::string new_value_;
    QWidget* input_widget_;
    mondoc::services::FillSessionService& service_;
    mondoc::FillSessionId session_id_;
};

}  // namespace mondoc::ui
