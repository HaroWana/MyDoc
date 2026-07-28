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
      field_id_(std::move(fieldId)),
      old_value_(std::move(oldValue)),
      new_value_(std::move(newValue)),
      input_widget_(inputWidget),
      service_(service),
      session_id_(std::move(sessionId)) {
    setText(QObject::tr("Edit %1").arg(fieldDisplayName));
}

void EditFieldCommand::redo() { applyValue(new_value_); }
void EditFieldCommand::undo() { applyValue(old_value_); }

bool EditFieldCommand::mergeWith(const QUndoCommand* other) {
    const auto* o = dynamic_cast<const EditFieldCommand*>(other);
    if (!o || o->field_id_ != field_id_) return false;
    new_value_ = o->new_value_;
    return true;
}

void EditFieldCommand::applyValue(const std::string& v) {
    auto write = service_.setFieldValue(session_id_, field_id_, v);
    if (!write) return;
    if (!input_widget_) return;
    QSignalBlocker block(input_widget_);
    if (auto* le = qobject_cast<QLineEdit*>(input_widget_)) {
        le->setText(QString::fromStdString(v));
    } else if (auto* te = qobject_cast<QTextEdit*>(input_widget_)) {
        te->setPlainText(QString::fromStdString(v));
    } else if (auto* de = qobject_cast<QDateEdit*>(input_widget_)) {
        de->setDate(QDate::fromString(QString::fromStdString(v),
                                      QStringLiteral("yyyy-MM-dd")));
    } else if (auto* cb = qobject_cast<QCheckBox*>(input_widget_)) {
        cb->setChecked(v == "true");
    } else if (auto* combo = qobject_cast<QComboBox*>(input_widget_)) {
        combo->setCurrentText(QString::fromStdString(v));
    }
}

}  // namespace mondoc::ui
