#pragma once

#include <atomic>
#include <string>
#include <vector>

#include <QObject>
#include <QString>

#include "ai_field_detector.hpp"
#include "domain/field.hpp"

namespace mondoc::ui {

class AiFieldDetectWorker : public QObject {
    Q_OBJECT
public:
    AiFieldDetectWorker(mondoc::adapters::ai::AiFieldDetector& detector,
                        std::string documentText,
                        std::vector<mondoc::domain::Field> existingFields,
                        QObject* parent = nullptr);

    void requestCancel() noexcept;

public slots:
    void run();

signals:
    void proposalsReady(std::vector<mondoc::domain::Field> newFields,
                        std::vector<mondoc::adapters::ai::FieldImprovement> improvements);
    void failed(QString message, int errorKind);
    void cancelled();

private:
    mondoc::adapters::ai::AiFieldDetector& detector_;
    std::string documentText_;
    std::vector<mondoc::domain::Field> existingFields_;
    std::atomic<bool> cancelled_{false};
};

}  // namespace mondoc::ui

Q_DECLARE_METATYPE(std::vector<mondoc::domain::Field>)
Q_DECLARE_METATYPE(std::vector<mondoc::adapters::ai::FieldImprovement>)
