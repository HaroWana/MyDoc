#include "chat_pane.hpp"

#include <utility>

#include <QFont>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QVBoxLayout>

namespace mondoc::ui {

ChatPane::ChatPane(QWidget* parent) : QWidget(parent) {
    setMinimumHeight(200);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);

    history_ = new QPlainTextEdit(this);
    history_->setReadOnly(true);
    history_->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    history_->setMaximumBlockCount(500);
    history_->setPlaceholderText(
        tr("Run \"Fill with AI\" to start \xe2\x80\x94 then refine with chat."));
    history_->setAccessibleName(tr("AI chat history"));

    auto* row = new QHBoxLayout();
    row->setSpacing(8);

    input_ = new QLineEdit(this);
    input_->setMaxLength(2000);
    input_->setPlaceholderText(
        tr("Type a refinement\xe2\x80\xa6 e.g. 'use ISO date format'"));
    input_->setAccessibleName(tr("Chat with AI"));

    sendBtn_ = new QPushButton(tr("Send Message"), this);
    sendBtn_->setStyleSheet(QStringLiteral(
        "background-color: #2563EB; color: white; padding: 6px 12px;"));
    sendBtn_->setAccessibleName(tr("Send chat message"));
    sendBtn_->setEnabled(false);

    row->addWidget(input_, 1);
    row->addWidget(sendBtn_, 0);

    layout->addWidget(history_, 1);
    layout->addLayout(row, 0);

    connect(input_, &QLineEdit::returnPressed, this, &ChatPane::onSendClicked);
    connect(sendBtn_, &QPushButton::clicked, this, &ChatPane::onSendClicked);
    connect(input_, &QLineEdit::textChanged, this, [this](const QString&) {
        sendBtn_->setEnabled(!busy_ && !input_->text().trimmed().isEmpty());
    });
}

void ChatPane::setLastPass1Facts(std::vector<mondoc::services::AiExtractedFact> facts) {
    lastPass1Facts_ = std::move(facts);
}

void ChatPane::appendMessage(const QString& prefix, const QString& body, bool italic) {
    QTextCursor cur = history_->textCursor();
    cur.movePosition(QTextCursor::End);

    QTextCharFormat boldFmt;
    boldFmt.setFontWeight(QFont::Bold);
    if (italic) boldFmt.setFontItalic(true);
    cur.insertText(prefix, boldFmt);

    QTextCharFormat regularFmt;
    regularFmt.setFontWeight(QFont::Normal);
    if (italic) regularFmt.setFontItalic(true);
    cur.insertText(body + QStringLiteral("\n"), regularFmt);

    history_->setTextCursor(cur);
    history_->ensureCursorVisible();
}

void ChatPane::appendUserMessage(const QString& text) {
    appendMessage(tr("You: "), text, false);
}

void ChatPane::appendAiMessage(const QString& text) {
    appendMessage(tr("AI: "), text, false);
}

void ChatPane::appendSystemMessage(const QString& text) {
    appendMessage(tr("[system] "), text, true);
}

void ChatPane::setBusy(bool busy) {
    busy_ = busy;
    input_->setEnabled(!busy);
    sendBtn_->setEnabled(!busy && !input_->text().trimmed().isEmpty());
}

QString ChatPane::currentInputText() const {
    return input_->text();
}

void ChatPane::clearInput() {
    input_->clear();
}

void ChatPane::clearHistory() {
    history_->clear();
    lastPass1Facts_.clear();
}

void ChatPane::onSendClicked() {
    if (busy_) return;
    const QString text = input_->text().trimmed();
    if (text.isEmpty()) return;
    appendUserMessage(text);
    clearInput();
    setBusy(true);
    emit refinementRequested(text, lastPass1Facts_);
}

}  // namespace mondoc::ui
