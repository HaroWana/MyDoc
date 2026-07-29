#pragma once
#include <QDialog>
#include <QStringList>
#include <filesystem>
#include "llm_config.hpp"

class QLineEdit;
class QDialogButtonBox;
class QLabel;
class QComboBox;
class QPushButton;
class QThread;

namespace mondoc::ui {

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(const mondoc::adapters::ai::LlmConfig& current,
                            QWidget* parent = nullptr);
    ~SettingsDialog() override;

signals:
    void settingsSaved(mondoc::adapters::ai::LlmConfig config);

private slots:
    void onSave();
    void onBrowseLibreOffice();
    void onRefreshModels();
    void onModelsFetched(int generation, QStringList models);
    void onModelsFailed(int generation, QString message, int errorKind);

private:
    void startModelFetch();
    void shutdownModelsThread();
    void finishModelsThread();

    QLineEdit* url_edit_;
    QLineEdit* key_edit_;
    QComboBox* model_combo_;
    QPushButton* refresh_models_btn_;
    QLineEdit* soffice_edit_;
    QLabel* helper_label_;
    QLabel* error_label_;
    QDialogButtonBox* buttons_;
    std::filesystem::path config_path_;
    QThread* models_thread_ = nullptr;
    int models_generation_ = 0;
};

}  // namespace mondoc::ui
