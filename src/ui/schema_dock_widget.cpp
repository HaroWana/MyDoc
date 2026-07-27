#include "schema_dock_widget.hpp"

#include <QComboBox>
#include <QFontDatabase>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QStyledItemDelegate>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QThread>
#include <QVBoxLayout>
#include <QWidget>

#include <uuid.h>

#include <algorithm>
#include <array>
#include <random>
#include <string>

#include "ai_field_detect_worker.hpp"
#include "llm_error.hpp"

namespace mondoc::ui {

namespace {

constexpr int kFieldIdRole       = Qt::UserRole + 1;
constexpr int kRowStateRole      = Qt::UserRole + 2;  // 0=Normal, 1=AiNew, 2=AiImproved
constexpr int kSuggestedNameRole = Qt::UserRole + 3;
constexpr int kSuggestedTypeRole = Qt::UserRole + 4;

std::string generateUuid() {
    static thread_local std::mt19937 generator{[] {
        std::random_device rd;
        std::array<std::seed_seq::result_type, std::mt19937::state_size> seed{};
        std::generate(seed.begin(), seed.end(), std::ref(rd));
        std::seed_seq seq(seed.begin(), seed.end());
        return std::mt19937{seq};
    }()};
    uuids::uuid_random_generator gen{generator};
    return uuids::to_string(gen());
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

QString accentStyle() {
    return QStringLiteral("QPushButton { background-color: #2563EB; color: white; "
                          "padding: 6px 12px; }");
}

SchemaDockWidget::SchemaDockWidget(QWidget* parent)
    : QDockWidget(parent),
      table_(new QTableWidget(this)),
      addFieldBtn_(new QPushButton(tr("Add Field"))),
      removeFieldBtn_(new QPushButton(tr("Remove Selected Field"))),
      discardBtn_(new QPushButton(tr("Discard Changes"))),
      saveBtn_(new QPushButton(tr("Save Schema"))),
      detectWithAiBtn_(new QPushButton(tr("Detect with AI"))),
      acceptProposalBtn_(new QPushButton(tr("Accept"))),
      discardProposalBtn_(new QPushButton(tr("Discard"))),
      aiStatusLabel_(new QLabel(tr("Detecting fields\xe2\x80\xa6"))),
      aiErrorLabel_(new QLabel()) {
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

    saveBtn_->setStyleSheet(accentStyle());
    saveBtn_->setAccessibleName(tr("Save Schema"));
    discardBtn_->setAccessibleName(tr("Discard Changes"));
    addFieldBtn_->setAccessibleName(tr("Add Field"));
    removeFieldBtn_->setAccessibleName(tr("Remove Selected Field"));
    removeFieldBtn_->setToolTip(tr("Remove Selected Field"));
    removeFieldBtn_->setEnabled(false);

    detectWithAiBtn_->setStyleSheet(accentStyle());
    detectWithAiBtn_->setAccessibleName(tr("Detect with AI"));
    detectWithAiBtn_->setEnabled(false);
    detectWithAiBtn_->setToolTip(
        tr("Configure the LLM endpoint in Settings to enable AI detection."));

    acceptProposalBtn_->setStyleSheet(accentStyle());
    acceptProposalBtn_->setAccessibleName(tr("Accept"));
    acceptProposalBtn_->setVisible(false);

    discardProposalBtn_->setAccessibleName(tr("Discard"));
    discardProposalBtn_->setVisible(false);

    aiStatusLabel_->setStyleSheet(QStringLiteral("color: gray; padding-right: 8px;"));
    aiStatusLabel_->setVisible(false);

    aiErrorLabel_->setStyleSheet(QStringLiteral("color: #DC2626;"));
    aiErrorLabel_->setVisible(false);
    aiErrorLabel_->setWordWrap(true);

    auto* container = new QWidget(this);
    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);
    layout->addWidget(table_);

    auto* fieldRow = new QHBoxLayout();
    fieldRow->addWidget(addFieldBtn_);
    fieldRow->addWidget(removeFieldBtn_);
    fieldRow->addWidget(detectWithAiBtn_);
    fieldRow->addWidget(acceptProposalBtn_);
    fieldRow->addWidget(discardProposalBtn_);
    fieldRow->addStretch(1);
    layout->addLayout(fieldRow);

    layout->addWidget(aiStatusLabel_);
    layout->addWidget(aiErrorLabel_);

    auto* sep = new QFrame();
    sep->setFrameShape(QFrame::HLine);
    sep->setFrameShadow(QFrame::Sunken);
    layout->addWidget(sep);

    auto* confirmRow = new QHBoxLayout();
    confirmRow->addWidget(discardBtn_);
    confirmRow->addStretch(1);
    confirmRow->addWidget(saveBtn_);
    layout->addLayout(confirmRow);

    setWidget(container);

    connect(addFieldBtn_, &QPushButton::clicked, this, &SchemaDockWidget::onAddField);
    connect(removeFieldBtn_, &QPushButton::clicked, this, &SchemaDockWidget::onRemoveField);
    connect(discardBtn_, &QPushButton::clicked, this, &SchemaDockWidget::schemaDiscarded);
    connect(saveBtn_, &QPushButton::clicked, this, &SchemaDockWidget::schemaSaved);
    connect(table_, &QTableWidget::itemSelectionChanged,
            this, &SchemaDockWidget::onSelectionChanged);
    connect(detectWithAiBtn_, &QPushButton::clicked,
            this, &SchemaDockWidget::onDetectWithAiClicked);
    connect(acceptProposalBtn_, &QPushButton::clicked,
            this, &SchemaDockWidget::onAcceptProposal);
    connect(discardProposalBtn_, &QPushButton::clicked,
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
    // finished->lambda that nulls aiThread_/aiWorker_ and stops the worker's
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
    shutdownThread(aiThread_, aiWorker_);
}

void SchemaDockWidget::setAiConfigured(bool configured) {
    detectWithAiBtn_->setEnabled(configured);
}

void SchemaDockWidget::setDocumentText(std::string text) {
    documentText_ = std::move(text);
}

void SchemaDockWidget::setDetector(mondoc::adapters::ai::AiFieldDetector* detector) {
    detector_ = detector;
}

void SchemaDockWidget::addFieldExternal(const mondoc::domain::Field& field) {
    const int row = table_->rowCount();
    table_->insertRow(row);
    setNameItem(row, field);
    setTypeItem(row, field.type_);
    table_->setCurrentCell(row, 0);
    onSelectionChanged();
}

void SchemaDockWidget::populate(const std::vector<mondoc::domain::Field>& fields) {
    table_->clearContents();
    table_->setRowCount(static_cast<int>(fields.size()));
    for (int row = 0; row < static_cast<int>(fields.size()); ++row) {
        setNameItem(row, fields[row]);
        setTypeItem(row, fields[row].type_);
    }
    onSelectionChanged();
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

        result.push_back(mondoc::domain::Field{
            mondoc::FieldId{id},
            name,
            indexToFieldType(typeIdx),
        });
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
    table_->removeRow(row);
    onSelectionChanged();
}

void SchemaDockWidget::onSelectionChanged() {
    const int row = table_->currentRow();
    removeFieldBtn_->setEnabled(row >= 0);
    const int state = (row >= 0 && table_->item(row, 0))
        ? table_->item(row, 0)->data(kRowStateRole).toInt() : 0;
    acceptProposalBtn_->setVisible(state != 0);
    discardProposalBtn_->setVisible(state != 0);
}

void SchemaDockWidget::restoreIdleButton() {
    detectWithAiBtn_->setText(tr("Detect with AI"));
    detectWithAiBtn_->setStyleSheet(accentStyle());
    aiStatusLabel_->setVisible(false);
}

void SchemaDockWidget::onDetectWithAiClicked() {
    if (aiThread_ != nullptr) {
        if (aiWorker_) aiWorker_->requestCancel();
        return;
    }
    if (detector_ == nullptr) return;

    aiErrorLabel_->setVisible(false);
    aiErrorLabel_->clear();

    aiWorker_ = new AiFieldDetectWorker(*detector_, documentText_, currentFields());
    aiThread_ = new QThread(this);
    aiWorker_->moveToThread(aiThread_);

    connect(aiThread_, &QThread::started, aiWorker_, &AiFieldDetectWorker::run);
    connect(aiWorker_, &AiFieldDetectWorker::proposalsReady,
            this, &SchemaDockWidget::onProposalsReady, Qt::QueuedConnection);
    connect(aiWorker_, &AiFieldDetectWorker::failed,
            this, &SchemaDockWidget::onDetectionFailed, Qt::QueuedConnection);
    connect(aiWorker_, &AiFieldDetectWorker::cancelled,
            this, &SchemaDockWidget::onDetectionCancelled, Qt::QueuedConnection);
    connect(aiWorker_, &AiFieldDetectWorker::proposalsReady, aiThread_, &QThread::quit);
    connect(aiWorker_, &AiFieldDetectWorker::failed,         aiThread_, &QThread::quit);
    connect(aiWorker_, &AiFieldDetectWorker::cancelled,      aiThread_, &QThread::quit);
    connect(aiThread_, &QThread::finished, aiWorker_, &QObject::deleteLater);
    connect(aiThread_, &QThread::finished, aiThread_, &QObject::deleteLater);
    connect(aiThread_, &QThread::finished, this, [this]() {
        aiThread_ = nullptr;
        aiWorker_ = nullptr;
    });

    detectWithAiBtn_->setText(tr("Cancel"));
    detectWithAiBtn_->setStyleSheet(QString{});
    aiStatusLabel_->setVisible(true);
    aiThread_->start();
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
            nameItem->setText(QStringLiteral("[AI] ") + QString::fromStdString(field.name_));
            nameItem->setData(kRowStateRole, 1);
        }
    }

    for (const auto& imp : improvements) {
        const std::string normalized = normalizeName(imp.field_name);
        for (int row = 0; row < table_->rowCount(); ++row) {
            auto* nameItem = table_->item(row, 0);
            if (!nameItem) continue;
            std::string cellText = nameItem->text().toStdString();
            if (cellText.rfind("[AI] ", 0) == 0) cellText = cellText.substr(5);
            if (normalizeName(cellText) != normalized) continue;

            nameItem->setBackground(QColor(0xFE, 0xF9, 0xC3));
            if (auto* typeItem = table_->item(row, 1)) {
                typeItem->setBackground(QColor(0xFE, 0xF9, 0xC3));
            }
            nameItem->setData(kRowStateRole, 2);
            nameItem->setData(kSuggestedNameRole,
                              QString::fromStdString(imp.suggested_name));
            nameItem->setData(kSuggestedTypeRole,
                              QString::fromStdString(imp.suggested_type));
            break;
        }
    }

    if (newFields.empty() && improvements.empty()) {
        aiStatusLabel_->setText(
            tr("AI found no additional fields. The detected schema looks complete."));
        aiStatusLabel_->setVisible(true);
    }

    onSelectionChanged();
}

void SchemaDockWidget::onDetectionFailed(QString message, int errorKind) {
    restoreIdleButton();

    const auto kind = static_cast<mondoc::adapters::ai::LlmError::Kind>(errorKind);
    QString errorText;
    switch (kind) {
        case mondoc::adapters::ai::LlmError::Kind::Unreachable:
            errorText = tr("LLM hub is unreachable. Check your API URL in Settings.");
            break;
        case mondoc::adapters::ai::LlmError::Kind::RateLimited:
            errorText = tr("LLM hub is rate-limiting requests. Try again in a moment.");
            break;
        case mondoc::adapters::ai::LlmError::Kind::BadResponse: {
            const std::string msg = message.toLower().toStdString();
            if (msg.find("context") != std::string::npos ||
                msg.find("length") != std::string::npos) {
                errorText = tr("Document is too large for AI field detection. "
                               "Register the template manually.");
            } else {
                errorText = tr("LLM hub returned an unexpected response. "
                               "Check the model name in Settings.");
            }
            break;
        }
        default:
            errorText = tr("LLM hub returned an unexpected response. "
                           "Check the model name in Settings.");
            break;
    }

    aiErrorLabel_->setText(errorText);
    aiErrorLabel_->setVisible(true);
}

void SchemaDockWidget::onDetectionCancelled() {
    restoreIdleButton();
}

void SchemaDockWidget::onAcceptProposal() {
    const int row = table_->currentRow();
    if (row < 0) return;
    auto* nameItem = table_->item(row, 0);
    if (!nameItem) return;

    const int state = nameItem->data(kRowStateRole).toInt();
    if (state == 1) {
        QString text = nameItem->text();
        if (text.startsWith(QStringLiteral("[AI] "))) {
            text = text.mid(5);
        }
        nameItem->setText(text);
        nameItem->setData(kRowStateRole, 0);
    } else if (state == 2) {
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
        nameItem->setData(kRowStateRole, 0);
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

    const int state = nameItem->data(kRowStateRole).toInt();
    if (state == 1) {
        table_->removeRow(row);
    } else if (state == 2) {
        nameItem->setBackground(QBrush{});
        if (auto* typeItem = table_->item(row, 1)) {
            typeItem->setBackground(QBrush{});
        }
        nameItem->setData(kRowStateRole, 0);
        nameItem->setData(kSuggestedNameRole, QVariant{});
        nameItem->setData(kSuggestedTypeRole, QVariant{});
    }

    onSelectionChanged();
}

bool SchemaDockWidget::nameExistsInTable(const std::string& normalizedName) const {
    for (int row = 0; row < table_->rowCount(); ++row) {
        auto* item = table_->item(row, 0);
        if (!item) continue;
        std::string text = item->text().toStdString();
        if (text.rfind("[AI] ", 0) == 0) text = text.substr(5);
        if (normalizeName(text) == normalizedName) return true;
    }
    return false;
}

}  // namespace mondoc::ui
