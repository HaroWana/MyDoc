#pragma once
#include <string>

#include <QObject>
#include <QStringList>

namespace mondoc::ui {

class ModelListWorker : public QObject {
    Q_OBJECT
public:
    ModelListWorker(std::string apiUrl, std::string apiKey,
                    QObject* parent = nullptr);

public slots:
    void run();  // emits exactly one of the two signals

signals:
    void finished(QStringList models);
    void failed(QString message, int errorKind);

private:
    std::string api_url_;
    std::string api_key_;
};

}  // namespace mondoc::ui
