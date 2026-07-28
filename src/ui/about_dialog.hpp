#pragma once
#include <QDialog>
#include <QList>
#include <QString>

class QListWidget;
class QTextBrowser;

namespace mondoc::ui {

class AboutDialog : public QDialog {
    Q_OBJECT
public:
    explicit AboutDialog(QWidget* parent = nullptr);

private slots:
    void onLibrarySelected(int row);

private:
    struct LibraryEntry {
        QString displayName;
        QString licenseFile;
    };

    QListWidget* library_list_;
    QTextBrowser* license_view_;
    QList<LibraryEntry> libraries_;
};

}  // namespace mondoc::ui
