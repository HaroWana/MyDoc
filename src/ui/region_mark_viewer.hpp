#pragma once
#include <QDialog>
#include <QString>
#include <optional>
#include <filesystem>
#include "domain/field.hpp"

class QLabel;
class QPushButton;
class QLineEdit;
class QComboBox;
class QScrollArea;
class QTextBrowser;

namespace mondoc::ui {

class RegionMarkViewer : public QDialog {
    Q_OBJECT
public:
    explicit RegionMarkViewer(const std::filesystem::path& sourcePath,
                              const QString& templateName,
                              QWidget* parent = nullptr);

    // Call after exec() == Accepted to retrieve the marked field.
    mondoc::domain::Field field() const;

private slots:
    void onConfirmRegion();
    void onSaveField();
    void onCloseWithoutSaving();

private:
    bool loadTextDocument(const std::filesystem::path& path);

    std::filesystem::path source_path_;
    std::optional<mondoc::domain::FieldLocation> pending_location_;
    mondoc::domain::Field field_;

    QLabel* instruction_label_;
    QScrollArea* scroll_area_;
    QTextBrowser* text_browser_;

    QLabel* name_prompt_label_;
    QLineEdit* name_edit_;
    QLabel* type_prompt_label_;
    QComboBox* type_combo_;

    QPushButton* confirm_region_btn_;
    QPushButton* save_field_btn_;
    QPushButton* close_btn_;
};

}  // namespace mondoc::ui
