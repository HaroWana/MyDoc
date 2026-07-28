#include "settings_dialog.hpp"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStandardPaths>
#include <QVBoxLayout>

#include "mondoc/expected.hpp"
#include "ui_style.hpp"

namespace mondoc::ui {

SettingsDialog::SettingsDialog(const mondoc::adapters::ai::LlmConfig& current,
                               QWidget* parent)
    : QDialog(parent),
      url_edit_(new QLineEdit(this)),
      key_edit_(new QLineEdit(this)),
      model_edit_(new QLineEdit(this)),
      helper_label_(new QLabel(tr("Changes take effect immediately \xe2\x80\x94 no restart required."), this)),
      error_label_(new QLabel(this)),
      buttons_(new QDialogButtonBox(
          QDialogButtonBox::Save | QDialogButtonBox::Cancel, this)) {
    setWindowTitle(tr("Settings"));
    setModal(true);
    setMinimumWidth(420);

    url_edit_->setText(QString::fromStdString(current.api_url_));
    url_edit_->setPlaceholderText(QStringLiteral("https://api.openai.com/v1"));
    url_edit_->setAccessibleName(tr("API URL"));

    key_edit_->setText(QString::fromStdString(current.api_key_));
    key_edit_->setPlaceholderText(QStringLiteral("sk-\xe2\x80\xa6"));
    key_edit_->setEchoMode(QLineEdit::Password);
    key_edit_->setAccessibleName(tr("API Key"));

    model_edit_->setText(QString::fromStdString(current.model_));
    model_edit_->setPlaceholderText(QStringLiteral("gpt-4o-mini"));
    model_edit_->setAccessibleName(tr("Model name"));

    error_label_->setVisible(false);
    error_label_->setStyleSheet(QStringLiteral("color: #DC2626;"));
    error_label_->setWordWrap(true);

    const QString configDir =
        QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    config_path_ = std::filesystem::path{configDir.toStdU16String()} / "config.json";

    auto* form = new QFormLayout;
    form->setSpacing(8);
    form->setContentsMargins(0, 0, 0, 0);
    form->addRow(tr("API URL:"), url_edit_);
    form->addRow(tr("API Key:"), key_edit_);
    form->addRow(tr("Model:"), model_edit_);

    if (auto* saveBtn = buttons_->button(QDialogButtonBox::Save)) {
        saveBtn->setStyleSheet(accentButtonStyle());
        saveBtn->setAccessibleName(tr("Save settings"));
    }
    if (auto* cancelBtn = buttons_->button(QDialogButtonBox::Cancel)) {
        cancelBtn->setAccessibleName(tr("Cancel"));
    }

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(24, 24, 24, 24);
    root->setSpacing(8);
    root->addLayout(form);
    root->addWidget(helper_label_);
    root->addWidget(error_label_);
    root->addWidget(buttons_);

    connect(buttons_, &QDialogButtonBox::accepted, this, &SettingsDialog::onSave);
    connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void SettingsDialog::onSave() {
    mondoc::adapters::ai::LlmConfig cfg;
    cfg.api_url_ = url_edit_->text().trimmed().toStdString();
    cfg.api_key_ = key_edit_->text().toStdString();
    cfg.model_ = model_edit_->text().trimmed().toStdString();

    const auto result = cfg.saveToJson(config_path_);
    if (!result) {
        const QString msg = tr("Could not save settings: %1. Settings were not applied.")
                                .arg(QString::fromStdString(result.error().message()));
        error_label_->setText(msg);
        error_label_->setVisible(true);
        return;
    }
    emit settingsSaved(cfg);
    accept();
}

}  // namespace mondoc::ui
