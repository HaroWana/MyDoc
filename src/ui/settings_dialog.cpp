#include "settings_dialog.hpp"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStandardPaths>
#include <QVBoxLayout>

#include "mondoc/expected.hpp"

namespace mondoc::ui {

SettingsDialog::SettingsDialog(const mondoc::adapters::ai::LlmConfig& current,
                               QWidget* parent)
    : QDialog(parent),
      urlEdit_(new QLineEdit(this)),
      keyEdit_(new QLineEdit(this)),
      modelEdit_(new QLineEdit(this)),
      helperLabel_(new QLabel(tr("Changes take effect immediately \xe2\x80\x94 no restart required."), this)),
      errorLabel_(new QLabel(this)),
      buttons_(new QDialogButtonBox(
          QDialogButtonBox::Save | QDialogButtonBox::Cancel, this)) {
    setWindowTitle(tr("Settings"));
    setModal(true);
    setMinimumWidth(420);

    urlEdit_->setText(QString::fromStdString(current.api_url));
    urlEdit_->setPlaceholderText(QStringLiteral("https://api.openai.com/v1"));
    urlEdit_->setAccessibleName(tr("API URL"));

    keyEdit_->setText(QString::fromStdString(current.api_key));
    keyEdit_->setPlaceholderText(QStringLiteral("sk-\xe2\x80\xa6"));
    keyEdit_->setEchoMode(QLineEdit::Password);
    keyEdit_->setAccessibleName(tr("API Key"));

    modelEdit_->setText(QString::fromStdString(current.model));
    modelEdit_->setPlaceholderText(QStringLiteral("gpt-4o-mini"));
    modelEdit_->setAccessibleName(tr("Model name"));

    errorLabel_->setVisible(false);
    errorLabel_->setStyleSheet(QStringLiteral("color: #DC2626;"));
    errorLabel_->setWordWrap(true);

    const QString configDir =
        QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    configPath_ = std::filesystem::path{configDir.toStdU16String()} / "config.json";

    auto* form = new QFormLayout;
    form->setSpacing(8);
    form->setContentsMargins(0, 0, 0, 0);
    form->addRow(tr("API URL:"), urlEdit_);
    form->addRow(tr("API Key:"), keyEdit_);
    form->addRow(tr("Model:"), modelEdit_);

    if (auto* saveBtn = buttons_->button(QDialogButtonBox::Save)) {
        saveBtn->setStyleSheet(
            QStringLiteral("QPushButton { background-color: #2563EB; color: white; "
                           "padding: 8px 16px; }"));
        saveBtn->setAccessibleName(tr("Save settings"));
    }
    if (auto* cancelBtn = buttons_->button(QDialogButtonBox::Cancel)) {
        cancelBtn->setAccessibleName(tr("Cancel"));
    }

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(24, 24, 24, 24);
    root->setSpacing(8);
    root->addLayout(form);
    root->addWidget(helperLabel_);
    root->addWidget(errorLabel_);
    root->addWidget(buttons_);

    connect(buttons_, &QDialogButtonBox::accepted, this, &SettingsDialog::onSave);
    connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void SettingsDialog::onSave() {
    mondoc::adapters::ai::LlmConfig cfg;
    cfg.api_url = urlEdit_->text().trimmed().toStdString();
    cfg.api_key = keyEdit_->text().toStdString();
    cfg.model = modelEdit_->text().trimmed().toStdString();

    const auto result = cfg.saveToJson(configPath_);
    if (!result) {
        const QString msg = tr("Could not save settings: %1. Settings were not applied.")
                                .arg(QString::fromStdString(result.error().message()));
        errorLabel_->setText(msg);
        errorLabel_->setVisible(true);
        return;
    }
    emit settingsSaved(cfg);
    accept();
}

}  // namespace mondoc::ui
