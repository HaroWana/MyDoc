#include "model_list_worker.hpp"

#include "llm_client.hpp"

namespace mondoc::ui {

ModelListWorker::ModelListWorker(std::string apiUrl, std::string apiKey,
                                 QObject* parent)
    : QObject(parent),
      api_url_(std::move(apiUrl)),
      api_key_(std::move(apiKey)) {}

void ModelListWorker::run() {
    auto client = mondoc::adapters::ai::LlmClient::create(api_url_, api_key_);
    if (!client) {
        emit failed(QString::fromStdString(client.error().message()),
                    static_cast<int>(client.error().kind()));
        return;
    }
    auto models = (*client)->listModels();
    if (!models) {
        emit failed(QString::fromStdString(models.error().message()),
                    static_cast<int>(models.error().kind()));
        return;
    }
    QStringList out;
    out.reserve(static_cast<qsizetype>(models->size()));
    for (const auto& id : *models) {
        out.append(QString::fromStdString(id));
    }
    emit finished(out);
}

}  // namespace mondoc::ui
