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

    QListWidget* libraryList_;
    QTextBrowser* licenseView_;
    QList<LibraryEntry> libraries_;
};

}  // namespace mondoc::ui
