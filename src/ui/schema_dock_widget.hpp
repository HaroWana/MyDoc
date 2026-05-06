#pragma once

#include <QDockWidget>
#include <vector>

#include "domain/field.hpp"

class QTableWidget;
class QPushButton;

namespace mondoc::ui {

class SchemaDockWidget : public QDockWidget {
    Q_OBJECT
public:
    explicit SchemaDockWidget(QWidget* parent = nullptr);

    void populate(const std::vector<mondoc::domain::Field>& fields);
    std::vector<mondoc::domain::Field> currentFields() const;

signals:
    void schemaSaved();
    void schemaDiscarded();

private slots:
    void onAddField();
    void onRemoveField();
    void onSelectionChanged();

private:
    void setNameItem(int row, const mondoc::domain::Field& field);
    void setTypeItem(int row, mondoc::domain::FieldType type);

    QTableWidget* table_;
    QPushButton* addFieldBtn_;
    QPushButton* removeFieldBtn_;
    QPushButton* discardBtn_;
    QPushButton* saveBtn_;
};

}  // namespace mondoc::ui
