#pragma once

#include <vector>

#include <QString>
#include <QWidget>

#include "fill_session_service.hpp"

class QLineEdit;
class QPlainTextEdit;
class QPushButton;

namespace mondoc::ui {

class ChatPane : public QWidget {
    Q_OBJECT
public:
    explicit ChatPane(QWidget* parent = nullptr);

    void setLastPass1Facts(std::vector<mondoc::domain::AiExtractedFact> facts);
    void appendUserMessage(const QString& text);
    void appendAiMessage(const QString& text);
    void appendSystemMessage(const QString& text);
    void setBusy(bool busy);
    QString currentInputText() const;
    void clearInput();
    void clearHistory();

signals:
    void refinementRequested(QString prompt,
                             std::vector<mondoc::domain::AiExtractedFact> lastFacts);

private slots:
    void onSendClicked();

private:
    void appendMessage(const QString& prefix, const QString& body, bool italic);

    QPlainTextEdit* history_ = nullptr;
    QLineEdit* input_ = nullptr;
    QPushButton* send_btn_ = nullptr;
    std::vector<mondoc::domain::AiExtractedFact> last_pass1_facts_;
    bool busy_ = false;
};

}  // namespace mondoc::ui

Q_DECLARE_METATYPE(std::vector<mondoc::domain::AiExtractedFact>)
