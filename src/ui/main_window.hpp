#pragma once

#include <QMainWindow>
#include <QString>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>

#include "ai_field_detector.hpp"
#include "domain/i_template_repository.hpp"
#include "domain/field.hpp"
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
class QThread;

namespace mondoc::ui {

class SchemaDockWidget;
class FillSessionView;
class ResumeBanner;
class DocumentCanvas;
class PreviewWorker;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(mondoc::services::TemplateService& templateService,
                        mondoc::services::FillSessionService& fillService,
                        mondoc::domain::ITemplateRepository& templateRepo,
                        const mondoc::adapters::ai::LlmConfig& currentConfig,
                        std::function<void(mondoc::adapters::ai::LlmConfig)> reconfigureLlmCallback,
                        std::filesystem::path dataDir,
                        QWidget* parent = nullptr);
    ~MainWindow() override;

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
    void onDetectFieldPositions();

    void onCanvasFrameDrawn(mondoc::domain::PdfLocation loc);
    void onCanvasFrameSelected(mondoc::FieldId id);
    void onCanvasFrameChanged(mondoc::FieldId id, mondoc::domain::PdfLocation loc);
    void onSchemaRowSelected(int row);

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

    void startPreview(const mondoc::TemplateId& templateId, const std::filesystem::path& source);
    void shutdownPreviewThread(bool mustJoin);
    void handlePreviewFinished(int generation, const QString& previewPdfPath, bool regenerated);
    void handlePreviewFailed(int generation, const QString& message);
    std::optional<mondoc::domain::TextLocation> computeTextAnchor(
        const mondoc::domain::PdfLocation& loc) const;

    mondoc::services::TemplateService& service_;
    mondoc::services::FillSessionService& fill_service_;
    mondoc::domain::ITemplateRepository& template_repo_;
    mondoc::adapters::ai::LlmConfig current_config_;
    std::function<void(mondoc::adapters::ai::LlmConfig)> reconfigure_llm_callback_;
    std::filesystem::path data_dir_;
    QAction* export_action_ = nullptr;

    QListWidget* template_list_;
    QStackedWidget* central_stack_;
    QLineEdit* search_box_;
    SchemaDockWidget* schema_widget_;
    FillSessionView* fill_session_view_;
    ResumeBanner* resume_banner_;
    DocumentCanvas* document_canvas_;

    QLabel* detail_name_label_;
    QLabel* detail_format_label_;
    QLabel* detail_field_count_label_;
    QLabel* detail_created_label_;
    QPushButton* detail_fill_btn_;
    QPushButton* detail_rename_btn_;
    QPushButton* detail_duplicate_btn_;
    QPushButton* detail_delete_btn_;
    QPushButton* detect_positions_btn_;

    mondoc::domain::Template pending_template_;
    std::string pending_document_text_;
    std::optional<mondoc::domain::Template> selected_template_;
    std::vector<mondoc::domain::Template> cached_templates_;

    QThread* preview_thread_ = nullptr;
    PreviewWorker* preview_worker_ = nullptr;
    int preview_generation_ = 0;
    bool preview_loaded_ = false;
    std::filesystem::path current_preview_path_;
};

}  // namespace mondoc::ui
