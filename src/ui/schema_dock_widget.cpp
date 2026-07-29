#include "schema_dock_widget.hpp"

#include "mondoc/util.hpp"

#include <QComboBox>
#include <QFontDatabase>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QStyledItemDelegate>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QThread>
#include <QVBoxLayout>
#include <QWidget>

#include <string>

#include "ai_field_detect_worker.hpp"
#include "llm_error.hpp"
#include "llm_error_text.hpp"
#include "ui_style.hpp"

namespace mondoc::ui {

namespace {

constexpr int kFieldIdRole       = Qt::UserRole + 1;
constexpr int kRowStateRole      = Qt::UserRole + 2;
constexpr int kSuggestedNameRole = Qt::UserRole + 3;
constexpr int kSuggestedTypeRole = Qt::UserRole + 4;

enum class AiRowState { None, Proposal, Improvement };

const QString kAiPrefix = QStringLiteral("[AI] ");

QString stripAiPrefix(const QString& text) {
    return text.startsWith(kAiPrefix) ? text.mid(kAiPrefix.size()) : text;
}

QStringList typeNames() {
    return {
        QObject::tr("Text"),
        QObject::tr("Paragraph"),
        QObject::tr("Number"),
        QObject::tr("Date"),
        QObject::tr("Checkbox"),
        QObject::tr("Dropdown"),
    };
}

int fieldTypeToIndex(mondoc::domain::FieldType t) {
    switch (t) {
        case mondoc::domain::FieldType::Text:      return 0;
        case mondoc::domain::FieldType::Paragraph: return 1;
        case mondoc::domain::FieldType::Number:    return 2;
        case mondoc::domain::FieldType::Date:      return 3;
        case mondoc::domain::FieldType::Checkbox:  return 4;
        case mondoc::domain::FieldType::Dropdown:  return 5;
    }
    return 0;
}

mondoc::domain::FieldType indexToFieldType(int i) {
    switch (i) {
        case 0: return mondoc::domain::FieldType::Text;
        case 1: return mondoc::domain::FieldType::Paragraph;
        case 2: return mondoc::domain::FieldType::Number;
        case 3: return mondoc::domain::FieldType::Date;
        case 4: return mondoc::domain::FieldType::Checkbox;
        case 5: return mondoc::domain::FieldType::Dropdown;
    }
    return mondoc::domain::FieldType::Text;
}

mondoc::domain::FieldType parseFieldType(const std::string& s) {
    if (s == "paragraph") return mondoc::domain::FieldType::Paragraph;
    if (s == "number")    return mondoc::domain::FieldType::Number;
    if (s == "date")      return mondoc::domain::FieldType::Date;
    if (s == "checkbox")  return mondoc::domain::FieldType::Checkbox;
    if (s == "dropdown")  return mondoc::domain::FieldType::Dropdown;
    return mondoc::domain::FieldType::Text;
}

std::string normalizeName(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == '_' || c == ' ') continue;
        out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

class FieldTypeDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QWidget* createEditor(QWidget* parent,
                          const QStyleOptionViewItem&,
                          const QModelIndex&) const override {
        auto* combo = new QComboBox(parent);
        combo->addItems(typeNames());
        return combo;
    }

    void setEditorData(QWidget* editor, const QModelIndex& index) const override {
        auto* combo = qobject_cast<QComboBox*>(editor);
        if (!combo) return;
        const int idx = index.data(Qt::EditRole).toInt();
        combo->setCurrentIndex(idx);
    }

    void setModelData(QWidget* editor,
                      QAbstractItemModel* model,
                      const QModelIndex& index) const override {
        auto* combo = qobject_cast<QComboBox*>(editor);
        if (!combo) return;
        model->setData(index, combo->currentIndex(), Qt::EditRole);
        model->setData(index, combo->currentText(), Qt::DisplayRole);
    }
};

}  // namespace

SchemaDockWidget::SchemaDockWidget(QWidget* parent)
    : QDockWidget(parent),
      table_(new QTableWidget(this)),
      add_field_btn_(new QPushButton(tr("Add Field"))),
      remove_field_btn_(new QPushButton(tr("Remove Selected Field"))),
      discard_btn_(new QPushButton(tr("Discard Changes"))),
      save_btn_(new QPushButton(tr("Save Schema"))),
      detect_with_ai_btn_(new QPushButton(tr("Detect with AI"))),
      accept_proposal_btn_(new QPushButton(tr("Accept"))),
      discard_proposal_btn_(new QPushButton(tr("Discard"))),
      ai_status_label_(new QLabel(tr("Detecting fields\xe2\x80\xa6"))),
      ai_error_label_(new QLabel()) {
    setMinimumWidth(320);
    setAllowedAreas(Qt::RightDockWidgetArea | Qt::LeftDockWidgetArea);

    table_->setColumnCount(2);
    table_->setHorizontalHeaderLabels({tr("Field Name"), tr("Type")});
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table_->setColumnWidth(1, 140);
    table_->verticalHeader()->setDefaultSectionSize(28);
    table_->verticalHeader()->setVisible(false);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setItemDelegateForColumn(1, new FieldTypeDelegate(this));
    table_->setAccessibleName(tr("Schema fields"));

    save_btn_->setStyleSheet(accentButtonStyle());
    save_btn_->setAccessibleName(tr("Save Schema"));
    discard_btn_->setAccessibleName(tr("Discard Changes"));
    add_field_btn_->setAccessibleName(tr("Add Field"));
    remove_field_btn_->setAccessibleName(tr("Remove Selected Field"));
    remove_field_btn_->setToolTip(tr("Remove Selected Field"));
    remove_field_btn_->setEnabled(false);

    detect_with_ai_btn_->setStyleSheet(accentButtonStyle());
    detect_with_ai_btn_->setAccessibleName(tr("Detect with AI"));
    detect_with_ai_btn_->setEnabled(false);
    detect_with_ai_btn_->setToolTip(
        tr("Configure the LLM endpoint in Settings to enable AI detection."));

    accept_proposal_btn_->setStyleSheet(accentButtonStyle());
    accept_proposal_btn_->setAccessibleName(tr("Accept"));
    accept_proposal_btn_->setVisible(false);

    discard_proposal_btn_->setAccessibleName(tr("Discard"));
    discard_proposal_btn_->setVisible(false);

    ai_status_label_->setStyleSheet(QStringLiteral("color: gray; padding-right: 8px;"));
    ai_status_label_->setVisible(false);

    ai_error_label_->setStyleSheet(QStringLiteral("color: #DC2626;"));
    ai_error_label_->setVisible(false);
    ai_error_label_->setWordWrap(true);

    auto* container = new QWidget(this);
    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);
    layout->addWidget(table_);

    auto* fieldRow = new QHBoxLayout();
    fieldRow->addWidget(add_field_btn_);
    fieldRow->addWidget(remove_field_btn_);
    fieldRow->addWidget(detect_with_ai_btn_);
    fieldRow->addWidget(accept_proposal_btn_);
    fieldRow->addWidget(discard_proposal_btn_);
    fieldRow->addStretch(1);
    layout->addLayout(fieldRow);

    layout->addWidget(ai_status_label_);
    layout->addWidget(ai_error_label_);

    auto* sep = new QFrame();
    sep->setFrameShape(QFrame::HLine);
    sep->setFrameShadow(QFrame::Sunken);
    layout->addWidget(sep);

    auto* confirmRow = new QHBoxLayout();
    confirmRow->addWidget(discard_btn_);
    confirmRow->addStretch(1);
    confirmRow->addWidget(save_btn_);
    layout->addLayout(confirmRow);

    setWidget(container);

    connect(add_field_btn_, &QPushButton::clicked, this, &SchemaDockWidget::onAddField);
    connect(remove_field_btn_, &QPushButton::clicked, this, &SchemaDockWidget::onRemoveField);
    connect(discard_btn_, &QPushButton::clicked, this, &SchemaDockWidget::schemaDiscarded);
    connect(save_btn_, &QPushButton::clicked, this, &SchemaDockWidget::onSaveClicked);
    connect(table_, &QTableWidget::itemSelectionChanged,
            this, &SchemaDockWidget::onSelectionChanged);
    connect(detect_with_ai_btn_, &QPushButton::clicked,
            this, &SchemaDockWidget::onDetectWithAiClicked);
    connect(accept_proposal_btn_, &QPushButton::clicked,
            this, &SchemaDockWidget::onAcceptProposal);
    connect(discard_proposal_btn_, &QPushButton::clicked,
            this, &SchemaDockWidget::onDiscardProposal);
}

void SchemaDockWidget::shutdownThread(QThread*& t, AiFieldDetectWorker*& worker) {
    // Only ever called from ~SchemaDockWidget() (there is no session-clear
    // equivalent for this dock), so this always blocks until the thread
    // actually exits rather than abandoning it on timeout: main.cpp destroys
    // the CompositionRoot (and with it the AI field detector) within a few
    // lines of destroying MainWindow, with no event-loop turn in between.
    // Abandoning here would let this worker's non-interruptible HTTP read (up
    // to 60s, llm_client's read timeout) keep running into an already-freed
    // detector.
    QThread* thread = t;
    if (!thread) return;

    // Sever delivery to `this` before anything else: kills the
    // finished->lambda that nulls ai_thread_/ai_worker_ and stops the worker's
    // own result signals (proposalsReady/failed/cancelled) from reaching
    // this dock. Connections from the worker to the thread (finished ->
    // thread->quit) are left intact so the thread still exits and its
    // finished->deleteLater cleanup still runs.
    if (worker) worker->disconnect(this);
    thread->disconnect(this);
    if (worker) worker->requestCancel();
    t = nullptr;
    worker = nullptr;

    if (!thread->isRunning()) return;

    thread->quit();
    if (!thread->wait(5000)) thread->wait();
}

SchemaDockWidget::~SchemaDockWidget() {
    shutdownThread(ai_thread_, ai_worker_);
}

void SchemaDockWidget::setAiConfigured(bool configured) {
    detect_with_ai_btn_->setEnabled(configured);
}

void SchemaDockWidget::setDocumentText(std::string text) {
    document_text_ = std::move(text);
}

void SchemaDockWidget::setDetector(mondoc::adapters::ai::AiFieldDetector* detector) {
    detector_ = detector;
}

void SchemaDockWidget::addFieldExternal(const mondoc::domain::Field& field) {
    const int row = table_->rowCount();
    table_->insertRow(row);
    setNameItem(row, field);
    setTypeItem(row, field.type_);
    if (field.location_) locations_[field.id_.value()] = *field.location_;
    table_->setCurrentCell(row, 0);
    onSelectionChanged();
}

void SchemaDockWidget::populate(const std::vector<mondoc::domain::Field>& fields) {
    // A repopulate is a programmatic resync, not a user selection — suppress
    // so a selection index left over from the previous content doesn't replay
    // as a spurious rowSelected against the new content (e.g. across template
    // switches).
    suppress_selection_signal_ = true;
    table_->clearContents();
    table_->setRowCount(static_cast<int>(fields.size()));
    locations_.clear();
    for (int row = 0; row < static_cast<int>(fields.size()); ++row) {
        setNameItem(row, fields[row]);
        setTypeItem(row, fields[row].type_);
        if (fields[row].location_) locations_[fields[row].id_.value()] = *fields[row].location_;
    }
    onSelectionChanged();
    suppress_selection_signal_ = false;
}

std::vector<mondoc::domain::Field> SchemaDockWidget::currentFields() const {
    std::vector<mondoc::domain::Field> result;
    const int rows = table_->rowCount();
    result.reserve(static_cast<std::size_t>(rows));
    for (int row = 0; row < rows; ++row) {
        auto* nameItem = table_->item(row, 0);
        auto* typeItem = table_->item(row, 1);
        if (!nameItem) continue;

        const std::string name = nameItem->text().trimmed().toStdString();
        if (name.empty()) continue;
        const QString idStr = nameItem->data(kFieldIdRole).toString();
        const std::string id = idStr.isEmpty() ? generateUuid() : idStr.toStdString();
        const int typeIdx = typeItem ? typeItem->data(Qt::EditRole).toInt() : 0;

        mondoc::domain::Field field{
            mondoc::FieldId{id},
            name,
            indexToFieldType(typeIdx),
        };
        if (auto it = locations_.find(id); it != locations_.end()) {
            field.location_ = it->second;
        }
        result.push_back(std::move(field));
    }
    return result;
}

void SchemaDockWidget::setNameItem(int row, const mondoc::domain::Field& field) {
    auto* item = new QTableWidgetItem(QString::fromStdString(field.name_));
    item->setFlags(item->flags() | Qt::ItemIsEditable);
    item->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    item->setData(kFieldIdRole, QString::fromStdString(field.id_.value()));
    table_->setItem(row, 0, item);
}

void SchemaDockWidget::setTypeItem(int row, mondoc::domain::FieldType type) {
    const int idx = fieldTypeToIndex(type);
    const QString display = typeNames().at(idx);
    auto* item = new QTableWidgetItem(display);
    item->setData(Qt::EditRole, idx);
    item->setFlags(item->flags() | Qt::ItemIsEditable);
    table_->setItem(row, 1, item);
}

void SchemaDockWidget::onAddField() {
    const int row = table_->rowCount();
    table_->insertRow(row);
    setNameItem(row, mondoc::domain::Field{mondoc::FieldId{}, std::string{},
                                           mondoc::domain::FieldType::Text});
    setTypeItem(row, mondoc::domain::FieldType::Text);
    table_->setCurrentCell(row, 0);
    table_->editItem(table_->item(row, 0));
}

void SchemaDockWidget::onRemoveField() {
    const int row = table_->currentRow();
    if (row < 0) return;
    if (auto* nameItem = table_->item(row, 0)) {
        const std::string id = nameItem->data(kFieldIdRole).toString().toStdString();
        if (!id.empty()) locations_.erase(id);
    }
    // Qt's row-removal reselection is an internal side effect, not a user
    // pick — suppress it and emit a single settled rowSelected below.
    suppress_selection_signal_ = true;
    table_->removeRow(row);
    suppress_selection_signal_ = false;
    onSelectionChanged();
}

void SchemaDockWidget::onSaveClicked() {
    for (int row = 0; row < table_->rowCount(); ++row) {
        auto* nameItem = table_->item(row, 0);
        const QString name = nameItem ? nameItem->text().trimmed() : QString();
        if (name.isEmpty()) {
            QMessageBox::warning(this, tr("Cannot Save Schema"),
                tr("Row %1 has no field name. Fill in a name or remove the row "
                   "before saving.").arg(row + 1));
            return;
        }
    }
    emit schemaSaved();
}

void SchemaDockWidget::onSelectionChanged() {
    const int row = table_->currentRow();
    remove_field_btn_->setEnabled(row >= 0);
    const auto state = (row >= 0 && table_->item(row, 0))
        ? static_cast<AiRowState>(table_->item(row, 0)->data(kRowStateRole).toInt())
        : AiRowState::None;
    accept_proposal_btn_->setVisible(state != AiRowState::None);
    discard_proposal_btn_->setVisible(state != AiRowState::None);
    if (!suppress_selection_signal_) emit rowSelected(row);
}

void SchemaDockWidget::selectRow(int row) {
    suppress_selection_signal_ = true;
    table_->setCurrentCell(row, 0);
    suppress_selection_signal_ = false;
}

void SchemaDockWidget::restoreIdleButton() {
    detect_with_ai_btn_->setText(tr("Detect with AI"));
    detect_with_ai_btn_->setStyleSheet(accentButtonStyle());
    ai_status_label_->setVisible(false);
}

void SchemaDockWidget::onDetectWithAiClicked() {
    if (ai_thread_ != nullptr) {
        if (ai_worker_) ai_worker_->requestCancel();
        return;
    }
    if (detector_ == nullptr) return;

    ai_error_label_->setVisible(false);
    ai_error_label_->clear();

    ai_worker_ = new AiFieldDetectWorker(*detector_, document_text_, currentFields());
    ai_thread_ = new QThread(this);
    ai_worker_->moveToThread(ai_thread_);

    connect(ai_thread_, &QThread::started, ai_worker_, &AiFieldDetectWorker::run);
    connect(ai_worker_, &AiFieldDetectWorker::proposalsReady,
            this, &SchemaDockWidget::onProposalsReady, Qt::QueuedConnection);
    connect(ai_worker_, &AiFieldDetectWorker::failed,
            this, &SchemaDockWidget::onDetectionFailed, Qt::QueuedConnection);
    connect(ai_worker_, &AiFieldDetectWorker::cancelled,
            this, &SchemaDockWidget::onDetectionCancelled, Qt::QueuedConnection);
    connect(ai_worker_, &AiFieldDetectWorker::proposalsReady, ai_thread_, &QThread::quit);
    connect(ai_worker_, &AiFieldDetectWorker::failed,         ai_thread_, &QThread::quit);
    connect(ai_worker_, &AiFieldDetectWorker::cancelled,      ai_thread_, &QThread::quit);
    connect(ai_thread_, &QThread::finished, ai_worker_, &QObject::deleteLater);
    connect(ai_thread_, &QThread::finished, ai_thread_, &QObject::deleteLater);
    connect(ai_thread_, &QThread::finished, this, [this]() {
        ai_thread_ = nullptr;
        ai_worker_ = nullptr;
    });

    detect_with_ai_btn_->setText(tr("Cancel"));
    detect_with_ai_btn_->setStyleSheet(QString{});
    ai_status_label_->setVisible(true);
    ai_thread_->start();
}

void SchemaDockWidget::onProposalsReady(
        std::vector<mondoc::domain::Field> newFields,
        std::vector<mondoc::adapters::ai::FieldImprovement> improvements) {
    restoreIdleButton();

    for (const auto& field : newFields) {
        const std::string normalized = normalizeName(field.name_);
        if (nameExistsInTable(normalized)) continue;

        const int row = table_->rowCount();
        table_->insertRow(row);
        setNameItem(row, field);
        setTypeItem(row, field.type_);

        auto* nameItem = table_->item(row, 0);
        if (nameItem) {
            nameItem->setText(kAiPrefix + QString::fromStdString(field.name_));
            nameItem->setData(kRowStateRole, static_cast<int>(AiRowState::Proposal));
        }
    }

    for (const auto& imp : improvements) {
        const std::string normalized = normalizeName(imp.field_name);
        for (int row = 0; row < table_->rowCount(); ++row) {
            auto* nameItem = table_->item(row, 0);
            if (!nameItem) continue;
            const std::string cellText = stripAiPrefix(nameItem->text()).toStdString();
            if (normalizeName(cellText) != normalized) continue;

            nameItem->setBackground(QColor(0xFE, 0xF9, 0xC3));
            if (auto* typeItem = table_->item(row, 1)) {
                typeItem->setBackground(QColor(0xFE, 0xF9, 0xC3));
            }
            nameItem->setData(kRowStateRole, static_cast<int>(AiRowState::Improvement));
            nameItem->setData(kSuggestedNameRole,
                              QString::fromStdString(imp.suggested_name));
            nameItem->setData(kSuggestedTypeRole,
                              QString::fromStdString(imp.suggested_type));
            break;
        }
    }

    if (newFields.empty() && improvements.empty()) {
        ai_status_label_->setText(
            tr("AI found no additional fields. The detected schema looks complete."));
        ai_status_label_->setVisible(true);
    }

    onSelectionChanged();
}

void SchemaDockWidget::onDetectionFailed(QString message, int errorKind) {
    restoreIdleButton();

    const auto llmKind = static_cast<mondoc::adapters::ai::LlmError::Kind>(errorKind);
    auto kind = mondoc::Error::Kind::BadResponse;
    switch (llmKind) {
        case mondoc::adapters::ai::LlmError::Kind::Unreachable:
            kind = mondoc::Error::Kind::Unreachable;
            break;
        case mondoc::adapters::ai::LlmError::Kind::RateLimited:
            kind = mondoc::Error::Kind::RateLimited;
            break;
        case mondoc::adapters::ai::LlmError::Kind::BadResponse:
            kind = mondoc::Error::Kind::BadResponse;
            break;
        case mondoc::adapters::ai::LlmError::Kind::Cancelled:
            kind = mondoc::Error::Kind::Cancelled;
            break;
    }

    ai_error_label_->setText(llmErrorText(mondoc::Error{kind, message.toStdString()}));
    ai_error_label_->setVisible(true);
}

void SchemaDockWidget::onDetectionCancelled() {
    restoreIdleButton();
}

void SchemaDockWidget::onAcceptProposal() {
    const int row = table_->currentRow();
    if (row < 0) return;
    auto* nameItem = table_->item(row, 0);
    if (!nameItem) return;

    const auto state = static_cast<AiRowState>(nameItem->data(kRowStateRole).toInt());
    if (state == AiRowState::Proposal) {
        nameItem->setText(stripAiPrefix(nameItem->text()));
        nameItem->setData(kRowStateRole, static_cast<int>(AiRowState::None));
    } else if (state == AiRowState::Improvement) {
        const QString suggestedName = nameItem->data(kSuggestedNameRole).toString();
        const QString suggestedType = nameItem->data(kSuggestedTypeRole).toString();

        if (!suggestedName.isEmpty()) {
            nameItem->setText(suggestedName);
        }
        if (!suggestedType.isEmpty()) {
            setTypeItem(row, parseFieldType(suggestedType.toStdString()));
        }

        nameItem->setBackground(QBrush{});
        if (auto* typeItem = table_->item(row, 1)) {
            typeItem->setBackground(QBrush{});
        }
        nameItem->setData(kRowStateRole, static_cast<int>(AiRowState::None));
        nameItem->setData(kSuggestedNameRole, QVariant{});
        nameItem->setData(kSuggestedTypeRole, QVariant{});
    }

    onSelectionChanged();
}

void SchemaDockWidget::onDiscardProposal() {
    const int row = table_->currentRow();
    if (row < 0) return;
    auto* nameItem = table_->item(row, 0);
    if (!nameItem) return;

    const auto state = static_cast<AiRowState>(nameItem->data(kRowStateRole).toInt());
    if (state == AiRowState::Proposal) {
        table_->removeRow(row);
    } else if (state == AiRowState::Improvement) {
        nameItem->setBackground(QBrush{});
        if (auto* typeItem = table_->item(row, 1)) {
            typeItem->setBackground(QBrush{});
        }
        nameItem->setData(kRowStateRole, static_cast<int>(AiRowState::None));
        nameItem->setData(kSuggestedNameRole, QVariant{});
        nameItem->setData(kSuggestedTypeRole, QVariant{});
    }

    onSelectionChanged();
}

bool SchemaDockWidget::nameExistsInTable(const std::string& normalizedName) const {
    for (int row = 0; row < table_->rowCount(); ++row) {
        auto* item = table_->item(row, 0);
        if (!item) continue;
        const std::string text = stripAiPrefix(item->text()).toStdString();
        if (normalizeName(text) == normalizedName) return true;
    }
    return false;
}

}  // namespace mondoc::ui
