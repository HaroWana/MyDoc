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

    const mondoc::FieldId& fieldId() const noexcept { return fieldId_; }

private:
    void applyValue(const std::string& value);

    mondoc::FieldId fieldId_;
    std::string oldValue_;
    std::string newValue_;
    QWidget* inputWidget_;
    mondoc::services::FillSessionService& service_;
    mondoc::FillSessionId sessionId_;
};

}  // namespace mondoc::ui
