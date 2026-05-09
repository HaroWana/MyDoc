#include "field_form_pane.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDate>
#include <QDateEdit>
#include <QFontDatabase>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QScrollArea>
#include <QString>
#include <QTextEdit>
#include <QTimer>
#include <QUndoStack>
#include <QVBoxLayout>

#include "edit_field_command.hpp"

namespace mondoc::ui {

namespace {

constexpr int kTextDebounceMs = 500;

QString fieldNameToDisplay(const std::string& name) {
    return QString::fromStdString(name);
}

}  // namespace

FieldFormPane::FieldFormPane(QWidget* parent)
    : QWidget(parent),
      scroll_(new QScrollArea(this)),
      form_(nullptr),
      formLayout_(nullptr) {
    scroll_->setWidgetResizable(true);
    scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll_->setAccessibleName(tr("Field form"));

    rebuildFormHost();

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->addWidget(scroll_);

    setMinimumWidth(280);
}

void FieldFormPane::rebuildFormHost() {
    form_ = new QWidget(scroll_);
    formLayout_ = new QVBoxLayout(form_);
    formLayout_->setContentsMargins(8, 8, 8, 8);
    formLayout_->setSpacing(8);
    formLayout_->addStretch(1);
    scroll_->setWidget(form_);
}

void FieldFormPane::clear() {
    lastCommitted_.clear();
    textEditTimers_.clear();
    rebuildFormHost();
}

void FieldFormPane::populate(const mondoc::domain::Template& tpl,
                             const mondoc::domain::FillSession& session,
                             mondoc::services::FillSessionService& service,
                             QUndoStack* undoStack,
                             mondoc::FillSessionId sessionId) {
    clear();
    service_ = &service;
    undoStack_ = undoStack;
    sessionId_ = std::move(sessionId);

    if (tpl.fields_.empty()) {
        auto* emptyLabel = new QLabel(tr("This template has no fields."), form_);
        emptyLabel->setAlignment(Qt::AlignCenter);
        formLayout_->insertWidget(0, emptyLabel);
        return;
    }

    for (const auto& field : tpl.fields_) {
        QString initial;
        for (const auto& fill : session.fills_) {
            if (fill.field_id_ == field.id_) {
                initial = QString::fromStdString(fill.current_value_);
                break;
            }
        }
        buildRow(field, initial);
        lastCommitted_[field.id_.value()] = initial;
    }
}

void FieldFormPane::buildRow(const mondoc::domain::Field& field,
                             const QString& initialValue) {
    auto* rowContainer = new QWidget(form_);
    auto* rowLayout = new QVBoxLayout(rowContainer);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->setSpacing(4);

    const QString displayName = fieldNameToDisplay(field.name_);
    QWidget* input = nullptr;

    using mondoc::domain::FieldType;
    switch (field.type_) {
        case FieldType::Text: {
            auto* le = new QLineEdit(rowContainer);
            le->setPlaceholderText(tr("Enter text\xe2\x80\xa6"));
            le->setText(initialValue);
            input = le;
            break;
        }
        case FieldType::Paragraph: {
            auto* te = new QTextEdit(rowContainer);
            te->setFixedHeight(80);
            te->setAcceptRichText(false);
            te->setPlaceholderText(tr("Enter text\xe2\x80\xa6"));
            te->setPlainText(initialValue);
            input = te;
            break;
        }
        case FieldType::Number: {
            auto* le = new QLineEdit(rowContainer);
            le->setValidator(new QIntValidator(le));
            le->setAlignment(Qt::AlignRight);
            le->setPlaceholderText(tr("0"));
            le->setText(initialValue);
            input = le;
            break;
        }
        case FieldType::Date: {
            auto* de = new QDateEdit(rowContainer);
            de->setCalendarPopup(true);
            de->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
            QDate parsed = QDate::fromString(initialValue, QStringLiteral("yyyy-MM-dd"));
            de->setDate(parsed.isValid() ? parsed : QDate::currentDate());
            input = de;
            break;
        }
        case FieldType::Checkbox: {
            auto* cb = new QCheckBox(displayName, rowContainer);
            cb->setChecked(initialValue == QStringLiteral("true"));
            input = cb;
            break;
        }
        case FieldType::Dropdown: {
            auto* combo = new QComboBox(rowContainer);
            if (!initialValue.isEmpty()) {
                combo->addItem(initialValue);
                combo->setCurrentText(initialValue);
            }
            input = combo;
            break;
        }
    }

    if (!input) return;

    input->setAccessibleName(displayName);

    if (field.type_ != FieldType::Checkbox) {
        auto* nameLabel = new QLabel(displayName, rowContainer);
        nameLabel->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        rowLayout->addWidget(nameLabel);
    }
    rowLayout->addWidget(input);

    if (!initialValue.isEmpty() ||
        (field.type_ == FieldType::Checkbox &&
         initialValue == QStringLiteral("true"))) {
        markFilled(input);
    }

    const mondoc::FieldId fieldId = field.id_;

    if (auto* le = qobject_cast<QLineEdit*>(input)) {
        connect(le, &QLineEdit::editingFinished, this,
                [this, fieldId, le, displayName]() {
                    commit(fieldId, le, displayName, le->text());
                });
    } else if (auto* te = qobject_cast<QTextEdit*>(input)) {
        auto* timer = new QTimer(te);
        timer->setSingleShot(true);
        textEditTimers_[fieldId.value()] = timer;
        connect(te, &QTextEdit::textChanged, this,
                [timer]() { timer->start(kTextDebounceMs); });
        connect(timer, &QTimer::timeout, this,
                [this, fieldId, te, displayName]() {
                    commit(fieldId, te, displayName, te->toPlainText());
                });
    } else if (auto* de = qobject_cast<QDateEdit*>(input)) {
        connect(de, &QDateEdit::dateChanged, this,
                [this, fieldId, de, displayName](const QDate& d) {
                    commit(fieldId, de, displayName,
                           d.toString(QStringLiteral("yyyy-MM-dd")));
                });
    } else if (auto* cb = qobject_cast<QCheckBox*>(input)) {
        connect(cb, &QCheckBox::stateChanged, this,
                [this, fieldId, cb, displayName](int state) {
                    commit(fieldId, cb, displayName,
                           state == Qt::Checked ? QStringLiteral("true")
                                                : QStringLiteral("false"));
                });
    } else if (auto* combo = qobject_cast<QComboBox*>(input)) {
        connect(combo,
                QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                [this, fieldId, combo, displayName](int) {
                    commit(fieldId, combo, displayName, combo->currentText());
                });
    }

    formLayout_->insertWidget(formLayout_->count() - 1, rowContainer);
}

void FieldFormPane::commit(const mondoc::FieldId& fieldId,
                           QWidget* input,
                           const QString& displayName,
                           const QString& newValue) {
    if (!service_ || !undoStack_) return;
    const auto key = fieldId.value();
    auto it = lastCommitted_.find(key);
    const QString oldValue = (it == lastCommitted_.end()) ? QString{} : it->second;
    if (oldValue == newValue) return;

    auto* cmd = new EditFieldCommand(fieldId,
                                     oldValue.toStdString(),
                                     newValue.toStdString(),
                                     input,
                                     *service_,
                                     sessionId_,
                                     displayName);
    undoStack_->push(cmd);
    lastCommitted_[key] = newValue;
    markFilled(input);
}

void FieldFormPane::markFilled(QWidget* input) {
    if (!input) return;
    input->setStyleSheet(QStringLiteral("border: 1px solid #2563EB;"));
}

}  // namespace mondoc::ui
