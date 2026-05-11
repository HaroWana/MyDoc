#include "field_form_pane.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDate>
#include <QDateEdit>
#include <QEvent>
#include <QFontDatabase>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QScrollArea>
#include <QSignalBlocker>
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
    sourceRefsByField_.clear();
    inputToFieldId_.clear();
    inputByField_.clear();
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
        mondoc::domain::Confidence initialConfidence = mondoc::domain::Confidence::Manual;
        for (const auto& fill : session.fills_) {
            if (fill.field_id_ == field.id_) {
                initial = QString::fromStdString(fill.current_value_);
                initialConfidence = fill.confidence_;
                sourceRefsByField_[field.id_.value()] = fill.source_refs_;
                break;
            }
        }
        buildRow(field, initial, initialConfidence);
        lastCommitted_[field.id_.value()] = initial;
    }
}

void FieldFormPane::buildRow(const mondoc::domain::Field& field,
                             const QString& initialValue,
                             mondoc::domain::Confidence initialConfidence) {
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
    input->installEventFilter(this);
    inputToFieldId_[input] = field.id_;
    inputByField_[field.id_.value()] = input;

    if (field.type_ != FieldType::Checkbox) {
        auto* nameLabel = new QLabel(displayName, rowContainer);
        nameLabel->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        rowLayout->addWidget(nameLabel);
    }
    rowLayout->addWidget(input);

    if (!initialValue.isEmpty() ||
        (field.type_ == FieldType::Checkbox &&
         initialValue == QStringLiteral("true"))) {
        markFilled(input, initialConfidence);
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
    markFilled(input, mondoc::domain::Confidence::Manual);
    sourceRefsByField_[key].clear();
}

void FieldFormPane::markFilled(QWidget* input, mondoc::domain::Confidence c) {
    if (!input) return;
    using mondoc::domain::Confidence;
    QString border;
    switch (c) {
        case Confidence::High:   border = QStringLiteral("border: 1px solid #22C55E;"); break;
        case Confidence::Medium: border = QStringLiteral("border: 1px solid #F59E0B;"); break;
        case Confidence::Low:    border = QStringLiteral("border: 1px solid #DC2626;"); break;
        case Confidence::Manual: border = QStringLiteral("border: 1px solid #2563EB;"); break;
    }
    input->setStyleSheet(border);
}

void FieldFormPane::populateAi(const std::vector<mondoc::domain::Fill>& fills) {
    using mondoc::domain::Confidence;
    for (const auto& f : fills) {
        if (f.confidence_ == Confidence::Manual) continue;
        auto it = inputByField_.find(f.field_id_.value());
        if (it == inputByField_.end()) continue;
        QWidget* input = it->second;
        const QString value = QString::fromStdString(f.current_value_);

        {
            QSignalBlocker blocker(input);
            if (auto* le = qobject_cast<QLineEdit*>(input))
                le->setText(value);
            else if (auto* te = qobject_cast<QTextEdit*>(input))
                te->setPlainText(value);
            else if (auto* de = qobject_cast<QDateEdit*>(input)) {
                QDate d = QDate::fromString(value, QStringLiteral("yyyy-MM-dd"));
                if (d.isValid()) de->setDate(d);
            } else if (auto* cb = qobject_cast<QCheckBox*>(input))
                cb->setChecked(value == QStringLiteral("true"));
            else if (auto* combo = qobject_cast<QComboBox*>(input)) {
                if (combo->findText(value) < 0) combo->addItem(value);
                combo->setCurrentText(value);
            }
        }

        markFilled(input, f.confidence_);
        sourceRefsByField_[f.field_id_.value()] = f.source_refs_;
        lastCommitted_[f.field_id_.value()] = value;
    }
}

bool FieldFormPane::eventFilter(QObject* obj, QEvent* ev) {
    if (ev->type() == QEvent::MouseButtonRelease) {
        auto* me = static_cast<QMouseEvent*>(ev);
        if (me->button() == Qt::LeftButton) {
            auto it = inputToFieldId_.find(obj);
            if (it != inputToFieldId_.end()) {
                auto refs = sourceRefsByField_.find(it->second.value());
                if (refs != sourceRefsByField_.end() && !refs->second.empty()) {
                    emit sourceRefRequested(refs->second.front());
                }
            }
        }
    }
    return false;
}

}  // namespace mondoc::ui
