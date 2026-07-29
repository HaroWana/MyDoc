#pragma once
#include <QDialog>
#include <filesystem>
#include "llm_config.hpp"

class QLineEdit;
class QDialogButtonBox;
class QLabel;

namespace mondoc::ui {

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(const mondoc::adapters::ai::LlmConfig& current,
                            QWidget* parent = nullptr);

signals:
    void settingsSaved(mondoc::adapters::ai::LlmConfig config);

private slots:
    void onSave();
    void onBrowseLibreOffice();

private:
    QLineEdit* url_edit_;
    QLineEdit* key_edit_;
    QLineEdit* model_edit_;
    QLineEdit* soffice_edit_;
    QLabel* helper_label_;
    QLabel* error_label_;
    QDialogButtonBox* buttons_;
    std::filesystem::path config_path_;
};

}  // namespace mondoc::ui
