#pragma once

#include <QDockWidget>
#include <string>
#include <vector>

#include "ai_field_detector.hpp"
#include "domain/field.hpp"

class QLabel;
class QTableWidget;
class QPushButton;
class QThread;

namespace mondoc::ui {

class AiFieldDetectWorker;

class SchemaDockWidget : public QDockWidget {
    Q_OBJECT
public:
    explicit SchemaDockWidget(QWidget* parent = nullptr);

    void populate(const std::vector<mondoc::domain::Field>& fields);
    void addFieldExternal(const mondoc::domain::Field& field);
    std::vector<mondoc::domain::Field> currentFields() const;

    void setAiConfigured(bool configured);
    void setDocumentText(std::string text);
    void setDetector(mondoc::adapters::ai::AiFieldDetector* detector);

signals:
    void schemaSaved();
    void schemaDiscarded();

private slots:
    void onAddField();
    void onRemoveField();
    void onSelectionChanged();
    void onDetectWithAiClicked();
    void onProposalsReady(std::vector<mondoc::domain::Field> newFields,
                          std::vector<mondoc::adapters::ai::FieldImprovement> improvements);
    void onDetectionFailed(QString message, int errorKind);
    void onDetectionCancelled();
    void onAcceptProposal();
    void onDiscardProposal();

private:
    void setNameItem(int row, const mondoc::domain::Field& field);
    void setTypeItem(int row, mondoc::domain::FieldType type);
    void restoreIdleButton();
    bool nameExistsInTable(const std::string& normalizedName) const;

    QTableWidget* table_;
    QPushButton* addFieldBtn_;
    QPushButton* removeFieldBtn_;
    QPushButton* discardBtn_;
    QPushButton* saveBtn_;
    QPushButton* detectWithAiBtn_;
    QPushButton* acceptProposalBtn_;
    QPushButton* discardProposalBtn_;
    QLabel* aiStatusLabel_;
    QLabel* aiErrorLabel_;

    std::string documentText_;
    QThread* aiThread_ = nullptr;
    AiFieldDetectWorker* aiWorker_ = nullptr;
    mondoc::adapters::ai::AiFieldDetector* detector_ = nullptr;
};

}  // namespace mondoc::ui
