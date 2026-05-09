#include "edit_field_command.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDate>
#include <QDateEdit>
#include <QLineEdit>
#include <QObject>
#include <QSignalBlocker>
#include <QString>
#include <QTextEdit>

namespace mondoc::ui {

EditFieldCommand::EditFieldCommand(mondoc::FieldId fieldId,
                                   std::string oldValue,
                                   std::string newValue,
                                   QWidget* inputWidget,
                                   mondoc::services::FillSessionService& service,
                                   mondoc::FillSessionId sessionId,
                                   QString fieldDisplayName,
                                   QUndoCommand* parent)
    : QUndoCommand(parent),
      fieldId_(std::move(fieldId)),
      oldValue_(std::move(oldValue)),
      newValue_(std::move(newValue)),
      inputWidget_(inputWidget),
      service_(service),
      sessionId_(std::move(sessionId)) {
    setText(QObject::tr("Edit %1").arg(fieldDisplayName));
}

void EditFieldCommand::redo() { applyValue(newValue_); }
void EditFieldCommand::undo() { applyValue(oldValue_); }

bool EditFieldCommand::mergeWith(const QUndoCommand* other) {
    const auto* o = dynamic_cast<const EditFieldCommand*>(other);
    if (!o || o->fieldId_ != fieldId_) return false;
    newValue_ = o->newValue_;
    return true;
}

void EditFieldCommand::applyValue(const std::string& v) {
    auto write = service_.setFieldValue(sessionId_, fieldId_, v);
    (void)write;
    if (!inputWidget_) return;
    QSignalBlocker block(inputWidget_);
    if (auto* le = qobject_cast<QLineEdit*>(inputWidget_)) {
        le->setText(QString::fromStdString(v));
    } else if (auto* te = qobject_cast<QTextEdit*>(inputWidget_)) {
        te->setPlainText(QString::fromStdString(v));
    } else if (auto* de = qobject_cast<QDateEdit*>(inputWidget_)) {
        de->setDate(QDate::fromString(QString::fromStdString(v),
                                      QStringLiteral("yyyy-MM-dd")));
    } else if (auto* cb = qobject_cast<QCheckBox*>(inputWidget_)) {
        cb->setChecked(v == "true");
    } else if (auto* combo = qobject_cast<QComboBox*>(inputWidget_)) {
        combo->setCurrentText(QString::fromStdString(v));
    }
}

}  // namespace mondoc::ui
