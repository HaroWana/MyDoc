#include "preview_worker.hpp"

#include <utility>

#include <QtGlobal>

#include "preview_provider.hpp"

namespace mondoc::ui {

PreviewWorker::PreviewWorker(std::filesystem::path source, std::string templateId,
                             std::filesystem::path cacheDir,
                             std::filesystem::path sofficePath, QObject* parent)
    : QObject(parent),
      source_(std::move(source)),
      template_id_(std::move(templateId)),
      cache_dir_(std::move(cacheDir)),
      soffice_path_(std::move(sofficePath)) {}

void PreviewWorker::run() {
    qInfo("PreviewWorker::run started");
    auto result = mondoc::adapters::formats::previewPdfFor(
        source_, template_id_, cache_dir_, soffice_path_);
    if (result) {
        qInfo("PreviewWorker::run finished");
        emit finished(QString::fromStdU16String(result->pdf_.u16string()),
                      result->regenerated_);
        return;
    }
    qInfo("PreviewWorker::run failed");
    emit failed(QString::fromStdString(result.error().message()));
}

}  // namespace mondoc::ui
