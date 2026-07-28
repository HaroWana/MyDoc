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
    ~SchemaDockWidget() override;

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
    void onSaveClicked();
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
    void shutdownThread(QThread*& t, AiFieldDetectWorker*& worker);

    QTableWidget* table_;
    QPushButton* add_field_btn_;
    QPushButton* remove_field_btn_;
    QPushButton* discard_btn_;
    QPushButton* save_btn_;
    QPushButton* detect_with_ai_btn_;
    QPushButton* accept_proposal_btn_;
    QPushButton* discard_proposal_btn_;
    QLabel* ai_status_label_;
    QLabel* ai_error_label_;

    std::string document_text_;
    QThread* ai_thread_ = nullptr;
    AiFieldDetectWorker* ai_worker_ = nullptr;
    mondoc::adapters::ai::AiFieldDetector* detector_ = nullptr;
};

}  // namespace mondoc::ui
