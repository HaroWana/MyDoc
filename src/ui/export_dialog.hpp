#pragma once

#include <QDialog>
#include <filesystem>

#include "fill_session_service.hpp"

class QComboBox;
class QLineEdit;
class QPushButton;
class QDialogButtonBox;

namespace mondoc::ui {

class ExportDialog : public QDialog {
    Q_OBJECT
public:
    explicit ExportDialog(QWidget* parent = nullptr);

    mondoc::services::ExportFormat selectedFormat() const;
    std::filesystem::path selectedPath() const;

private slots:
    void onBrowse();
    void onFormatChanged(int index);
    void updateOkEnabled();

private:
    QComboBox* format_combo_;
    QLineEdit* dest_edit_;
    QPushButton* browse_btn_;
    QDialogButtonBox* buttons_;
};

}  // namespace mondoc::ui
