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

#include "ui_style.hpp"

namespace mondoc::ui {

namespace {

struct FormatEntry {
    mondoc::services::ExportFormat format;
    const char* label;
    const char* filter;
};

const FormatEntry kFormats[] = {
    {mondoc::services::ExportFormat::Docx,     "DOCX",       "DOCX (*.docx)"},
    {mondoc::services::ExportFormat::Pdf,      "PDF",        "PDF (*.pdf)"},
    {mondoc::services::ExportFormat::Text,     "Plain Text", "Text (*.txt *.md)"},
    {mondoc::services::ExportFormat::Odt,      "ODT",        "ODT (*.odt)"},
};

}  // namespace

ExportDialog::ExportDialog(QWidget* parent)
    : QDialog(parent),
      format_combo_(new QComboBox(this)),
      dest_edit_(new QLineEdit(this)),
      browse_btn_(new QPushButton(tr("Browse\xe2\x80\xa6"), this)),
      buttons_(new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                     this)) {
    setWindowTitle(tr("Export Document"));
    setModal(true);

    for (const auto& f : kFormats) {
        format_combo_->addItem(tr(f.label));
    }
    dest_edit_->setReadOnly(true);
    dest_edit_->setAccessibleName(tr("Export destination"));
    format_combo_->setAccessibleName(tr("Export format"));
    browse_btn_->setAccessibleName(tr("Browse for export destination"));

    auto* form = new QFormLayout();
    form->addRow(tr("Format:"), format_combo_);
    auto* destRow = new QHBoxLayout();
    destRow->addWidget(dest_edit_);
    destRow->addWidget(browse_btn_);
    form->addRow(tr("Destination:"), destRow);

    auto* root = new QVBoxLayout(this);
    root->addLayout(form);
    root->addWidget(buttons_);

    if (auto* okBtn = buttons_->button(QDialogButtonBox::Ok)) {
        okBtn->setText(tr("Export"));
        okBtn->setStyleSheet(accentButtonStyle());
    }
    if (auto* cancelBtn = buttons_->button(QDialogButtonBox::Cancel)) {
        cancelBtn->setText(tr("Cancel Export"));
    }

    connect(buttons_, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(browse_btn_, &QPushButton::clicked, this, &ExportDialog::onBrowse);
    connect(format_combo_,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ExportDialog::onFormatChanged);
    connect(dest_edit_, &QLineEdit::textChanged,
            this, &ExportDialog::updateOkEnabled);

    updateOkEnabled();
}

mondoc::services::ExportFormat ExportDialog::selectedFormat() const {
    return kFormats[format_combo_->currentIndex()].format;
}

std::filesystem::path ExportDialog::selectedPath() const {
    return std::filesystem::path{dest_edit_->text().toStdU16String()};
}

void ExportDialog::onBrowse() {
    const auto& f = kFormats[format_combo_->currentIndex()];
    const QString chosen = QFileDialog::getSaveFileName(
        this, tr("Export Document"), QString{}, tr(f.filter));
    if (!chosen.isEmpty()) {
        dest_edit_->setText(chosen);
    }
}

void ExportDialog::onFormatChanged(int) {
    const auto& f = kFormats[format_combo_->currentIndex()];
    const auto ext = mondoc::services::exportFormatExtension(f.format);
    const QString suffix = QString::fromLatin1(ext.data(),
                                               static_cast<qsizetype>(ext.size()));
    if (!dest_edit_->text().isEmpty() &&
        !dest_edit_->text().endsWith(suffix, Qt::CaseInsensitive)) {
        dest_edit_->clear();
    }
    updateOkEnabled();
}

void ExportDialog::updateOkEnabled() {
    if (auto* okBtn = buttons_->button(QDialogButtonBox::Ok)) {
        okBtn->setEnabled(!dest_edit_->text().trimmed().isEmpty());
    }
}

}  // namespace mondoc::ui
