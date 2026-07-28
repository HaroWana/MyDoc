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
class QPdfDocument;
class QScrollArea;
class QTextBrowser;

namespace mondoc::ui {

class PdfPageWidget;  // internal, defined in region_mark_viewer.cpp

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
    void onRegionDrawn(const QRect& rect);

private:
    bool loadPdf(const std::filesystem::path& path);
    bool renderPdfPage(int pageIndex);
    bool loadTextDocument(const std::filesystem::path& path);

    std::filesystem::path source_path_;
    QPdfDocument* pdf_doc_ = nullptr;
    int pdf_page_index_ = 0;
    std::optional<mondoc::domain::FieldLocation> pending_location_;
    bool region_drawn_ = false;
    mondoc::domain::Field field_;

    QLabel* instruction_label_;
    QScrollArea* scroll_area_;
    QTextBrowser* text_browser_;
    PdfPageWidget* pdf_widget_;

    QLabel* name_prompt_label_;
    QLineEdit* name_edit_;
    QLabel* type_prompt_label_;
    QComboBox* type_combo_;

    QPushButton* confirm_region_btn_;
    QPushButton* save_field_btn_;
    QPushButton* close_btn_;

    QPushButton* prev_page_btn_ = nullptr;
    QPushButton* next_page_btn_ = nullptr;
    QLabel* page_indicator_ = nullptr;
};

}  // namespace mondoc::ui
