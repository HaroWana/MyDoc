#pragma once

#include <filesystem>
#include <string>

#include <QObject>
#include <QString>

namespace mondoc::ui {

class PreviewWorker : public QObject {
    Q_OBJECT
public:
    PreviewWorker(std::filesystem::path source, std::string templateId,
                  std::filesystem::path cacheDir, std::filesystem::path sofficePath,
                  QObject* parent = nullptr);

public slots:
    void run();

signals:
    void finished(QString previewPdfPath, bool regenerated);
    void failed(QString message);

private:
    std::filesystem::path source_;
    std::string template_id_;
    std::filesystem::path cache_dir_;
    std::filesystem::path soffice_path_;
};

}  // namespace mondoc::ui
