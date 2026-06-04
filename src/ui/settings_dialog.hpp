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

private:
    QLineEdit* urlEdit_;
    QLineEdit* keyEdit_;
    QLineEdit* modelEdit_;
    QLabel* helperLabel_;
    QLabel* errorLabel_;
    QDialogButtonBox* buttons_;
    std::filesystem::path configPath_;
};

}  // namespace mondoc::ui
