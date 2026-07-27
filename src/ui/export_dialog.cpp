#include "export_dialog.hpp"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QString>
#include <QVBoxLayout>

namespace mondoc::ui {

namespace {

struct FormatEntry {
    mondoc::services::ExportFormat format;
    const char* label;
    const char* extension;
    const char* filter;
};

const FormatEntry kFormats[] = {
    {mondoc::services::ExportFormat::Docx,     "DOCX",       ".docx", "DOCX (*.docx)"},
    {mondoc::services::ExportFormat::Pdf,      "PDF",        ".pdf",  "PDF (*.pdf)"},
    {mondoc::services::ExportFormat::Text,     "Plain Text", ".txt",  "Text (*.txt *.md)"},
    {mondoc::services::ExportFormat::Odt,      "ODT",        ".odt",  "ODT (*.odt)"},
};

}  // namespace

ExportDialog::ExportDialog(QWidget* parent)
    : QDialog(parent),
      formatCombo_(new QComboBox(this)),
      destEdit_(new QLineEdit(this)),
      browseBtn_(new QPushButton(tr("Browse\xe2\x80\xa6"), this)),
      buttons_(new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                     this)) {
    setWindowTitle(tr("Export Document"));
    setModal(true);

    for (const auto& f : kFormats) {
        formatCombo_->addItem(QString::fromUtf8(f.label));
    }
    destEdit_->setReadOnly(true);
    destEdit_->setAccessibleName(tr("Export destination"));
    formatCombo_->setAccessibleName(tr("Export format"));
    browseBtn_->setAccessibleName(tr("Browse for export destination"));

    auto* form = new QFormLayout();
    form->addRow(tr("Format:"), formatCombo_);
    auto* destRow = new QHBoxLayout();
    destRow->addWidget(destEdit_);
    destRow->addWidget(browseBtn_);
    form->addRow(tr("Destination:"), destRow);

    auto* root = new QVBoxLayout(this);
    root->addLayout(form);
    root->addWidget(buttons_);

    if (auto* okBtn = buttons_->button(QDialogButtonBox::Ok)) {
        okBtn->setText(tr("Export"));
        okBtn->setStyleSheet(
            QStringLiteral("QPushButton { background-color: #2563EB; color: white; "
                           "padding: 6px 12px; }"));
    }
    if (auto* cancelBtn = buttons_->button(QDialogButtonBox::Cancel)) {
        cancelBtn->setText(tr("Cancel Export"));
    }

    connect(buttons_, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(browseBtn_, &QPushButton::clicked, this, &ExportDialog::onBrowse);
    connect(formatCombo_,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ExportDialog::onFormatChanged);
    connect(destEdit_, &QLineEdit::textChanged,
            this, &ExportDialog::updateOkEnabled);

    updateOkEnabled();
}

mondoc::services::ExportFormat ExportDialog::selectedFormat() const {
    return kFormats[formatCombo_->currentIndex()].format;
}

std::filesystem::path ExportDialog::selectedPath() const {
    return std::filesystem::path{destEdit_->text().toStdU16String()};
}

void ExportDialog::onBrowse() {
    const auto& f = kFormats[formatCombo_->currentIndex()];
    const QString chosen = QFileDialog::getSaveFileName(
        this, tr("Export Document"), QString{}, QString::fromUtf8(f.filter));
    if (!chosen.isEmpty()) {
        destEdit_->setText(chosen);
    }
}

void ExportDialog::onFormatChanged(int) {
    const auto& f = kFormats[formatCombo_->currentIndex()];
    if (!destEdit_->text().isEmpty() &&
        !destEdit_->text().endsWith(QString::fromUtf8(f.extension), Qt::CaseInsensitive)) {
        destEdit_->clear();
    }
    updateOkEnabled();
}

void ExportDialog::updateOkEnabled() {
    if (auto* okBtn = buttons_->button(QDialogButtonBox::Ok)) {
        okBtn->setEnabled(!destEdit_->text().trimmed().isEmpty());
    }
}

}  // namespace mondoc::ui
