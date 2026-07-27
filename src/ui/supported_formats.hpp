#pragma once

#include <QCoreApplication>
#include <QString>
#include <QStringList>

namespace mondoc::ui {

inline const QStringList& acceptedExtensions() {
    static const QStringList exts{
        QStringLiteral("docx"),
        QStringLiteral("odt"),
        QStringLiteral("pdf"),
        QStringLiteral("txt"),
        QStringLiteral("md"),
    };
    return exts;
}

inline QString registrationDialogFilter() {
    return QCoreApplication::translate("MainWindow", "Documents (*.docx *.odt *.pdf *.txt *.md)");
}

inline QString attachSourcesDialogFilter() {
    return QCoreApplication::translate("MainWindow",
                                       "Source documents (*.docx *.odt *.pdf *.txt *.md)");
}

}  // namespace mondoc::ui
