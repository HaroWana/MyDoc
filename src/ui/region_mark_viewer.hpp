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

    std::filesystem::path sourcePath_;
    QPdfDocument* pdfDoc_ = nullptr;
    int pdfPageIndex_ = 0;
    std::optional<mondoc::domain::FieldLocation> pendingLocation_;
    bool regionDrawn_ = false;
    mondoc::domain::Field field_;

    QLabel* instructionLabel_;
    QScrollArea* scrollArea_;
    QTextBrowser* textBrowser_;
    PdfPageWidget* pdfWidget_;

    QLabel* namePromptLabel_;
    QLineEdit* nameEdit_;
    QLabel* typePromptLabel_;
    QComboBox* typeCombo_;

    QPushButton* confirmRegionBtn_;
    QPushButton* saveFieldBtn_;
    QPushButton* closeBtn_;

    QPushButton* prevPageBtn_ = nullptr;
    QPushButton* nextPageBtn_ = nullptr;
    QLabel* pageIndicator_ = nullptr;
};

}  // namespace mondoc::ui
