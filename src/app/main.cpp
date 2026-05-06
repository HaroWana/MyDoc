#include <QApplication>
#include <QDir>
#include <QMessageBox>
#include <QStandardPaths>
#include <QString>

#include <filesystem>
#include <utility>

#include "composition_root.hpp"
#include "main_window.hpp"
#include "migrations.hpp"
#include "sqlite_connection.hpp"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("mondoc"));
    QApplication::setOrganizationName(QStringLiteral("mondoc"));
    QApplication::setStyle(QStringLiteral("fusion"));

    const QString appDataDirQs =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (appDataDirQs.isEmpty()) {
        QMessageBox::critical(nullptr, QStringLiteral("MonDoc"),
            QObject::tr("Cannot determine application data directory."));
        return 1;
    }
    QDir().mkpath(appDataDirQs);

    const std::filesystem::path appDataDir{appDataDirQs.toStdU16String()};
    const std::filesystem::path dbPath = appDataDir / "mondoc.db";

    auto connResult = mondoc::adapters::storage::SqliteConnection::open(dbPath);
    if (!connResult) {
        QMessageBox::critical(nullptr, QStringLiteral("MonDoc"),
            QObject::tr("Cannot open database: %1")
                .arg(QString::fromStdString(connResult.error().message())));
        return 1;
    }

    auto migResult = mondoc::adapters::storage::runMigrations(*connResult);
    if (!migResult) {
        QMessageBox::critical(nullptr, QStringLiteral("MonDoc"),
            QObject::tr("Database migration failed: %1")
                .arg(QString::fromStdString(migResult.error().message())));
        return 1;
    }

    mondoc::app::CompositionRoot root{std::move(*connResult)};
    mondoc::ui::MainWindow window{root.service_};
    window.resize(1024, 768);
    window.show();

    return app.exec();
}
