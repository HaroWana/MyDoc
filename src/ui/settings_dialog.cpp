#include "settings_dialog.hpp"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QThread>
#include <QVBoxLayout>

#include "llm_error.hpp"
#include "llm_error_text.hpp"
#include "model_list_worker.hpp"
#include "mondoc/expected.hpp"
#include "ui_style.hpp"

namespace {
constexpr auto kSettingsOrg = "MonDoc";
constexpr auto kSettingsApp = "MonDoc";
constexpr auto kSofficeKey = "libreoffice/path";
}  // namespace

namespace mondoc::ui {

SettingsDialog::SettingsDialog(const mondoc::adapters::ai::LlmConfig& current,
                               QWidget* parent)
    : QDialog(parent),
      url_edit_(new QLineEdit(this)),
      key_edit_(new QLineEdit(this)),
      model_combo_(new QComboBox(this)),
      refresh_models_btn_(new QPushButton(this)),
      soffice_edit_(new QLineEdit(this)),
      helper_label_(new QLabel(tr("Changes take effect immediately \xe2\x80\x94 no restart required."), this)),
      error_label_(new QLabel(this)),
      buttons_(new QDialogButtonBox(
          QDialogButtonBox::Save | QDialogButtonBox::Cancel, this)) {
    setWindowTitle(tr("Settings"));
    setModal(true);
    setMinimumWidth(420);

    url_edit_->setText(QString::fromStdString(current.api_url_));
    url_edit_->setPlaceholderText(QStringLiteral("https://api.openai.com"));
    url_edit_->setAccessibleName(tr("API URL"));

    key_edit_->setText(QString::fromStdString(current.api_key_));
    key_edit_->setPlaceholderText(QStringLiteral("sk-\xe2\x80\xa6"));
    key_edit_->setEchoMode(QLineEdit::Password);
    key_edit_->setAccessibleName(tr("API Key"));

    model_combo_->setEditable(true);
    model_combo_->setInsertPolicy(QComboBox::NoInsert);
    model_combo_->setCurrentText(QString::fromStdString(current.model_));
    model_combo_->lineEdit()->setPlaceholderText(QStringLiteral("gpt-4o-mini"));
    model_combo_->setAccessibleName(tr("Model name"));

    refresh_models_btn_->setText(tr("Refresh"));
    refresh_models_btn_->setAccessibleName(tr("Refresh model list"));
    connect(refresh_models_btn_, &QPushButton::clicked,
            this, &SettingsDialog::onRefreshModels);

    auto* modelRow = new QHBoxLayout;
    modelRow->setContentsMargins(0, 0, 0, 0);
    modelRow->addWidget(model_combo_, 1);
    modelRow->addWidget(refresh_models_btn_);

    const QSettings appSettings(QString::fromLatin1(kSettingsOrg), QString::fromLatin1(kSettingsApp));
    soffice_edit_->setText(appSettings.value(QString::fromLatin1(kSofficeKey)).toString());
    soffice_edit_->setPlaceholderText(tr("Auto-detect"));
    soffice_edit_->setAccessibleName(tr("LibreOffice path"));

    auto* browseSofficeBtn = new QPushButton(tr("Browse\xe2\x80\xa6"), this);
    browseSofficeBtn->setAccessibleName(tr("Browse for LibreOffice"));
    connect(browseSofficeBtn, &QPushButton::clicked,
            this, &SettingsDialog::onBrowseLibreOffice);

    auto* sofficeRow = new QHBoxLayout;
    sofficeRow->setContentsMargins(0, 0, 0, 0);
    sofficeRow->addWidget(soffice_edit_);
    sofficeRow->addWidget(browseSofficeBtn);

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
    form->addRow(tr("Model:"), modelRow);
    form->addRow(tr("LibreOffice path:"), sofficeRow);

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

    if (!url_edit_->text().trimmed().isEmpty() && !key_edit_->text().isEmpty()) {
        startModelFetch();
    }
}

SettingsDialog::~SettingsDialog() {
    shutdownModelsThread();
}

void SettingsDialog::shutdownModelsThread() {
    if (!models_thread_) return;
    models_thread_->quit();
    // Blocking close on an in-flight fetch is bounded by the client's
    // 10 s connect / 30 s read timeouts (C-3: never destroy a running QThread).
    models_thread_->wait();
    models_thread_->deleteLater();
    models_thread_ = nullptr;
}

void SettingsDialog::onRefreshModels() {
    startModelFetch();
}

void SettingsDialog::startModelFetch() {
    if (models_thread_) return;  // one fetch at a time; button is disabled anyway

    const int generation = ++models_generation_;
    refresh_models_btn_->setEnabled(false);
    refresh_models_btn_->setToolTip(tr("Fetching models\xe2\x80\xa6"));

    auto* thread = new QThread(this);
    auto* worker = new ModelListWorker(url_edit_->text().trimmed().toStdString(),
                                       key_edit_->text().toStdString());
    worker->moveToThread(thread);
    connect(thread, &QThread::started, worker, &ModelListWorker::run);
    connect(worker, &ModelListWorker::finished, this,
            [this, generation](QStringList models) {
                onModelsFetched(generation, std::move(models));
            });
    connect(worker, &ModelListWorker::failed, this,
            [this, generation](QString message, int errorKind) {
                onModelsFailed(generation, std::move(message), errorKind);
            });
    connect(worker, &ModelListWorker::finished, thread, &QThread::quit);
    connect(worker, &ModelListWorker::failed, thread, &QThread::quit);
    connect(thread, &QThread::finished, worker, &QObject::deleteLater);
    models_thread_ = thread;
    thread->start();
}

void SettingsDialog::onModelsFetched(int generation, QStringList models) {
    finishModelsThread();
    if (generation != models_generation_) return;
    const QString previous = model_combo_->currentText();
    model_combo_->clear();
    model_combo_->addItems(models);
    model_combo_->setCurrentText(previous);  // selects if present, keeps as text otherwise
    error_label_->setVisible(false);
}

void SettingsDialog::onModelsFailed(int generation, QString message, int errorKind) {
    finishModelsThread();
    if (generation != models_generation_) return;

    const auto llmKind = static_cast<mondoc::adapters::ai::LlmError::Kind>(errorKind);
    auto kind = mondoc::Error::Kind::BadResponse;
    switch (llmKind) {
        case mondoc::adapters::ai::LlmError::Kind::Unreachable:
            kind = mondoc::Error::Kind::Unreachable;
            break;
        case mondoc::adapters::ai::LlmError::Kind::RateLimited:
            kind = mondoc::Error::Kind::RateLimited;
            break;
        case mondoc::adapters::ai::LlmError::Kind::BadResponse:
            kind = mondoc::Error::Kind::BadResponse;
            break;
        case mondoc::adapters::ai::LlmError::Kind::Cancelled:
            kind = mondoc::Error::Kind::Cancelled;
            break;
    }

    error_label_->setText(tr("Could not fetch models: %1")
                               .arg(llmErrorText(mondoc::Error{kind, message.toStdString()})));
    error_label_->setVisible(true);
}

void SettingsDialog::finishModelsThread() {
    refresh_models_btn_->setEnabled(true);
    refresh_models_btn_->setToolTip(QString());
    if (models_thread_) {
        models_thread_->quit();
        models_thread_->wait();
        models_thread_->deleteLater();
        models_thread_ = nullptr;
    }
}

void SettingsDialog::onSave() {
    mondoc::adapters::ai::LlmConfig cfg;
    cfg.api_url_ = url_edit_->text().trimmed().toStdString();
    cfg.api_key_ = key_edit_->text().toStdString();
    cfg.model_ = model_combo_->currentText().trimmed().toStdString();

    const auto result = cfg.saveToJson(config_path_);
    if (!result) {
        const QString msg = tr("Could not save settings: %1. Settings were not applied.")
                                .arg(QString::fromStdString(result.error().message()));
        error_label_->setText(msg);
        error_label_->setVisible(true);
        return;
    }

    QSettings appSettings(QString::fromLatin1(kSettingsOrg), QString::fromLatin1(kSettingsApp));
    appSettings.setValue(QString::fromLatin1(kSofficeKey), soffice_edit_->text().trimmed());

    emit settingsSaved(cfg);
    accept();
}

void SettingsDialog::onBrowseLibreOffice() {
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Select LibreOffice Executable"), soffice_edit_->text());
    if (path.isEmpty()) return;
    soffice_edit_->setText(path);
}

}  // namespace mondoc::ui
