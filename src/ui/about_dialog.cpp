#include "about_dialog.hpp"

#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QFile>
#include <QFont>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QTextBrowser>
#include <QVBoxLayout>

namespace mondoc::ui {

AboutDialog::AboutDialog(QWidget* parent)
    : QDialog(parent),
      library_list_(new QListWidget(this)),
      license_view_(new QTextBrowser(this))
{
    setWindowTitle(tr("About MonDoc"));
    setModal(true);
    setMinimumSize(640, 420);

    libraries_ = {
        {QStringLiteral("Qt6"),          QStringLiteral("Qt6-LGPL-3.0.txt")},
        {QStringLiteral("PoDoFo"),        QStringLiteral("podofo-LGPL.txt")},
        {QStringLiteral("libzip"),        QStringLiteral("libzip-BSD-3.txt")},
        {QStringLiteral("pugixml"),       QStringLiteral("pugixml-MIT.txt")},
        {QStringLiteral("nlohmann/json"), QStringLiteral("nlohmann_json-MIT.txt")},
        {QStringLiteral("SQLiteCpp"),     QStringLiteral("SQLiteCpp-MIT.txt")},
        {QStringLiteral("tl::expected"),  QStringLiteral("tl_expected-CC0.txt")},
        {QStringLiteral("zlib"),          QStringLiteral("zlib-license.txt")},
        {QStringLiteral("stduuid"),       QStringLiteral("stduuid-MIT.txt")},
    };

    for (const auto& lib : libraries_)
        library_list_->addItem(lib.displayName);
    library_list_->setAccessibleName(tr("Third-party libraries"));

    license_view_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    license_view_->setReadOnly(true);
    license_view_->setPlaceholderText(tr("Select a library to view its license."));

    auto* productLabel = new QLabel(
        QStringLiteral("MonDoc \xe2\x80\x94 Accurate, human-reviewed document filling."), this);
    {
        QFont f = productLabel->font();
        f.setBold(true);
        f.setPointSize(f.pointSize() + 2);
        productLabel->setFont(f);
    }

    auto* versionLabel = new QLabel(QStringLiteral("Version 1.0 \xc2\xb7 Built 2026"), this);

    auto* lgplNotice = new QLabel(
        tr("Qt is used under LGPL v3. Qt libraries are shipped as separate dynamic libraries "
           "and may be replaced by the user."), this);
    lgplNotice->setWordWrap(true);

    auto* libsHeading = new QLabel(tr("Third-party Libraries"), this);
    {
        QFont f = libsHeading->font();
        f.setBold(true);
        libsHeading->setFont(f);
    }

    auto* licenseHeading = new QLabel(tr("License Text"), this);
    {
        QFont f = licenseHeading->font();
        f.setBold(true);
        licenseHeading->setFont(f);
    }

    auto* closeBox = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(closeBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* leftPane = new QVBoxLayout;
    leftPane->addWidget(libsHeading);
    leftPane->addWidget(library_list_);

    auto* rightPane = new QVBoxLayout;
    rightPane->addWidget(licenseHeading);
    rightPane->addWidget(license_view_);

    auto* paneRow = new QHBoxLayout;
    paneRow->addLayout(leftPane, 1);
    paneRow->addLayout(rightPane, 2);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(24, 24, 24, 24);
    root->setSpacing(8);
    root->addWidget(productLabel);
    root->addWidget(versionLabel);
    root->addWidget(lgplNotice);
    root->addLayout(paneRow, 1);
    root->addWidget(closeBox);

    connect(library_list_, &QListWidget::currentRowChanged,
            this, &AboutDialog::onLibrarySelected);
}

void AboutDialog::onLibrarySelected(int row) {
    if (row < 0 || static_cast<qsizetype>(row) >= libraries_.size()) return;
    const QString licenseDir =
        QCoreApplication::applicationDirPath() + QStringLiteral("/LICENSES/");
    QFile f(licenseDir + libraries_.at(row).licenseFile);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        license_view_->setPlainText(
            tr("License file not found: %1").arg(libraries_.at(row).licenseFile));
        return;
    }
    license_view_->setPlainText(QString::fromUtf8(f.readAll()));
}

}  // namespace mondoc::ui
