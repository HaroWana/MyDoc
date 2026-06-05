#include <QApplication>
#include <QDir>
#include <QMessageBox>
#include <QStandardPaths>
#include <QString>

#include <filesystem>
#include <utility>

#include "ai_fill_worker.hpp"
#include "chat_pane.hpp"
#include "composition_root.hpp"
#include "main_window.hpp"
#include "migrations.hpp"
#include "sqlite_connection.hpp"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("mondoc"));
    QApplication::setOrganizationName(QStringLiteral("mondoc"));
    QApplication::setStyle(QStringLiteral("fusion"));

    qRegisterMetaType<std::vector<mondoc::domain::Fill>>();
    qRegisterMetaType<std::vector<mondoc::services::AiExtractedFact>>();

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

    const QString configDirQs =
        QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    const std::filesystem::path configPath =
        std::filesystem::path{configDirQs.toStdU16String()} / "config.json";
    auto cfgResult = mondoc::adapters::ai::LlmConfig::loadFromJson(configPath);
    if (!cfgResult) {
        QMessageBox::critical(nullptr, QStringLiteral("MonDoc"),
            QObject::tr("Cannot load AI configuration: %1")
                .arg(QString::fromStdString(cfgResult.error().message())));
        return 1;
    }

    mondoc::app::CompositionRoot root{std::move(*connResult), std::move(*cfgResult)};
    mondoc::ui::MainWindow window{root.service_,
                                  root.fill_session_service_,
                                  root.repo_,
                                  root.config(),
                                  [&root](mondoc::adapters::ai::LlmConfig cfg) {
                                      root.reconfigureLlm(std::move(cfg));
                                  }};
    window.resize(1024, 768);
    window.show();

    return app.exec();
}
