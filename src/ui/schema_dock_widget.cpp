#include "schema_dock_widget.hpp"

#include <QComboBox>
#include <QFontDatabase>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QPushButton>
#include <QStyledItemDelegate>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <QWidget>

#include <uuid.h>

#include <algorithm>
#include <array>
#include <random>
#include <string>

namespace mondoc::ui {

namespace {

constexpr int kFieldIdRole = Qt::UserRole + 1;

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
      addFieldBtn_(new QPushButton(tr("Add Field"))),
      removeFieldBtn_(new QPushButton(tr("Remove Selected Field"))),
      discardBtn_(new QPushButton(tr("Discard Changes"))),
      saveBtn_(new QPushButton(tr("Save Schema"))) {
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

    saveBtn_->setStyleSheet(
        QStringLiteral("QPushButton { background-color: #2563EB; color: white; "
                       "padding: 6px 12px; }"));
    saveBtn_->setAccessibleName(tr("Save Schema"));
    discardBtn_->setAccessibleName(tr("Discard Changes"));
    addFieldBtn_->setAccessibleName(tr("Add Field"));
    removeFieldBtn_->setAccessibleName(tr("Remove Selected Field"));
    removeFieldBtn_->setToolTip(tr("Remove Selected Field"));
    removeFieldBtn_->setEnabled(false);

    auto* container = new QWidget(this);
    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);
    layout->addWidget(table_);

    auto* fieldRow = new QHBoxLayout();
    fieldRow->addWidget(addFieldBtn_);
    fieldRow->addWidget(removeFieldBtn_);
    fieldRow->addStretch(1);
    layout->addLayout(fieldRow);

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

        const QString idStr = nameItem->data(kFieldIdRole).toString();
        const std::string id = idStr.isEmpty() ? generateUuid() : idStr.toStdString();
        const std::string name = nameItem->text().toStdString();
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
    removeFieldBtn_->setEnabled(table_->currentRow() >= 0);
}

}  // namespace mondoc::ui
