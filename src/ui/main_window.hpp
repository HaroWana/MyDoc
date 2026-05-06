#pragma once

#include <QMainWindow>
#include <filesystem>
#include <optional>

#include "domain/template.hpp"
#include "mondoc/id.hpp"
#include "services/template_service.hpp"

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

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(mondoc::services::TemplateService& service,
                        QWidget* parent = nullptr);

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

private:
    void buildSidebar(QWidget* sidebar);
    void buildEmptyState(QWidget* page);
    void buildDetailPage(QWidget* page);
    void refreshTemplateList();
    void triggerRegistration(const std::filesystem::path& path);
    void setDropHighlight(bool active);
    bool acceptableDrop(const QList<QUrl>& urls) const;
    std::optional<mondoc::TemplateId> selectedTemplateId() const;

    mondoc::services::TemplateService& service_;

    QListWidget* templateList_;
    QStackedWidget* centralStack_;
    QLineEdit* searchBox_;
    SchemaDockWidget* schemaWidget_;

    QLabel* detailNameLabel_;
    QLabel* detailFormatLabel_;
    QLabel* detailFieldCountLabel_;
    QLabel* detailCreatedLabel_;
    QPushButton* detailRenameBtn_;
    QPushButton* detailDuplicateBtn_;
    QPushButton* detailDeleteBtn_;

    mondoc::domain::Template pendingTemplate_;
    std::optional<mondoc::domain::Template> selectedTemplate_;
};

}  // namespace mondoc::ui
