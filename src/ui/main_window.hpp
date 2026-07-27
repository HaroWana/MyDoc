#pragma once

#include <QMainWindow>
#include <QString>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>

#include "ai_field_detector.hpp"
#include "domain/i_template_repository.hpp"
#include "domain/template.hpp"
#include "fill_session_service.hpp"
#include "llm_config.hpp"
#include "mondoc/id.hpp"
#include "template_service.hpp"

class QListWidget;
class QStackedWidget;
class QLineEdit;
class QLabel;
class QPushButton;
class QDragEnterEvent;
class QDragMoveEvent;
class QDragLeaveEvent;
class QDropEvent;

namespace mondoc::ui {

class SchemaDockWidget;
class FillSessionView;
class ResumeBanner;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(mondoc::services::TemplateService& templateService,
                        mondoc::services::FillSessionService& fillService,
                        mondoc::domain::ITemplateRepository& templateRepo,
                        const mondoc::adapters::ai::LlmConfig& currentConfig,
                        std::function<void(mondoc::adapters::ai::LlmConfig)> reconfigureLlmCallback,
                        QWidget* parent = nullptr);

    void setAiFieldDetector(mondoc::adapters::ai::AiFieldDetector* detector);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private slots:
    void onRegisterClicked();
    void onSchemaSaved();
    void onSchemaDiscarded();
    void onTemplateSelected(int row);
    void onSearchChanged(const QString& text);
    void onRenameTemplate();
    void onDuplicateTemplate();
    void onDeleteTemplate();
    void showListContextMenu(const QPoint& pos);

    void onFillSessionClicked();
    void onSessionBackRequested();
    void onSessionExported(QString fileName);
    void onSessionExportFailed(QString message);
    void onResumeRequested(mondoc::FillSessionId sessionId);
    void onDiscardRequested(mondoc::FillSessionId sessionId);

    void onSettingsClicked();
    void onExportTemplate();
    void onImportTemplate();
    void onMarkRegion();
    void onAboutClicked();

private:
    void buildSidebar(QWidget* sidebar);
    void buildEmptyState(QWidget* page);
    void buildDetailPage(QWidget* page);
    void refreshTemplateList();
    void refreshResumeBanner();
    QString relativeTimestamp(std::int64_t updatedAtUnix) const;
    void triggerRegistration(const std::filesystem::path& path);
    void setDropHighlight(bool active);
    bool acceptableDrop(const QList<QUrl>& urls) const;
    std::optional<mondoc::TemplateId> selectedTemplateId() const;

    mondoc::services::TemplateService& service_;
    mondoc::services::FillSessionService& fillService_;
    mondoc::domain::ITemplateRepository& templateRepo_;
    mondoc::adapters::ai::LlmConfig currentConfig_;
    std::function<void(mondoc::adapters::ai::LlmConfig)> reconfigureLlmCallback_;
    QAction* exportAction_ = nullptr;

    QListWidget* templateList_;
    QStackedWidget* centralStack_;
    QLineEdit* searchBox_;
    SchemaDockWidget* schemaWidget_;
    FillSessionView* fillSessionView_;
    ResumeBanner* resumeBanner_;

    QLabel* detailNameLabel_;
    QLabel* detailFormatLabel_;
    QLabel* detailFieldCountLabel_;
    QLabel* detailCreatedLabel_;
    QPushButton* detailFillBtn_;
    QPushButton* detailRenameBtn_;
    QPushButton* detailDuplicateBtn_;
    QPushButton* detailDeleteBtn_;

    mondoc::domain::Template pendingTemplate_;
    std::string pendingDocumentText_;
    std::optional<mondoc::domain::Template> selectedTemplate_;
    std::vector<mondoc::domain::Template> cachedTemplates_;
};

}  // namespace mondoc::ui
