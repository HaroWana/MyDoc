#include "import_conflict_dialog.hpp"
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include "ui_style.hpp"

namespace mondoc::ui {

ImportConflictDialog::ImportConflictDialog(const QString& conflictingName, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Template Name Already Exists"));
    setModal(true);

    auto* bodyLabel = new QLabel(
        tr("A template named \"%1\" is already in your library. What would you like to do?")
            .arg(conflictingName), this);
    bodyLabel->setWordWrap(true);

    auto* warningLabel = new QLabel(
        tr("Overwriting will permanently delete the existing template and all its fields."), this);
    warningLabel->setWordWrap(true);
    warningLabel->setStyleSheet(QStringLiteral("color: #DC2626;"));

    auto* overwriteBtn = new QPushButton(tr("Overwrite"), this);
    overwriteBtn->setStyleSheet(accentButtonStyle());
    overwriteBtn->setAccessibleName(tr("Overwrite existing template"));

    auto* copyBtn = new QPushButton(tr("Import as Copy"), this);
    copyBtn->setStyleSheet(accentButtonStyle());
    copyBtn->setAccessibleName(tr("Import as a copy with a new name"));

    auto* cancelBtn = new QPushButton(tr("Cancel Import"), this);
    cancelBtn->setAccessibleName(tr("Cancel import"));

    connect(overwriteBtn, &QPushButton::clicked, this, [this]() {
        choice_ = ConflictChoice::Overwrite;
        accept();
    });
    connect(copyBtn, &QPushButton::clicked, this, [this]() {
        choice_ = ConflictChoice::Copy;
        accept();
    });
    connect(cancelBtn, &QPushButton::clicked, this, [this]() {
        choice_ = ConflictChoice::Cancel;
        reject();
    });

    auto* btnRow = new QHBoxLayout;
    btnRow->addStretch(1);
    btnRow->addWidget(cancelBtn);
    btnRow->addWidget(copyBtn);
    btnRow->addWidget(overwriteBtn);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(24, 24, 24, 24);
    root->setSpacing(8);
    root->addWidget(bodyLabel);
    root->addWidget(warningLabel);
    root->addLayout(btnRow);
}

}  // namespace mondoc::ui
