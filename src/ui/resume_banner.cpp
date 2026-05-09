#include "resume_banner.hpp"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayoutItem>
#include <QMessageBox>
#include <QPushButton>
#include <QString>
#include <QStringLiteral>
#include <QVBoxLayout>
#include <QWidget>

#include <cstddef>

namespace mondoc::ui {

ResumeBanner::ResumeBanner(QWidget* parent)
    : QFrame(parent),
      layout_(new QVBoxLayout(this)) {
    setFrameShape(QFrame::StyledPanel);
    setFrameShadow(QFrame::Plain);
    layout_->setContentsMargins(8, 4, 8, 4);
    layout_->setSpacing(4);
    setVisible(false);
}

void ResumeBanner::setDrafts(const std::vector<DraftSummary>& drafts) {
    clearRows();
    if (drafts.empty()) {
        setVisible(false);
        return;
    }
    for (std::size_t i = 0; i < drafts.size(); ++i) {
        const auto& d = drafts[i];
        auto* row = new QWidget(this);
        auto* rowLayout = new QVBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(4);

        auto* label = new QLabel(
            QStringLiteral("%1 \xe2\x80\x94 %2")
                .arg(d.templateName, d.relativeTimestamp),
            row);
        label->setAccessibleName(tr("Draft session"));
        label->setWordWrap(true);
        rowLayout->addWidget(label);

        auto* btnRow = new QWidget(row);
        auto* btnLayout = new QHBoxLayout(btnRow);
        btnLayout->setContentsMargins(0, 0, 0, 0);
        btnLayout->setSpacing(8);

        const auto sid = d.sessionId;
        auto* resumeBtn = new QPushButton(tr("Resume Session"), btnRow);
        resumeBtn->setAccessibleName(tr("Resume Session"));
        connect(resumeBtn, &QPushButton::clicked, this, [this, sid]() {
            emit resumeRequested(sid);
        });

        auto* discardBtn = new QPushButton(tr("Discard Draft"), btnRow);
        discardBtn->setStyleSheet(QStringLiteral("color: #DC2626;"));
        discardBtn->setAccessibleName(tr("Discard Draft"));
        connect(discardBtn, &QPushButton::clicked, this, [this, sid]() {
            confirmAndEmitDiscard(sid);
        });

        btnLayout->addWidget(resumeBtn);
        btnLayout->addWidget(discardBtn);
        btnLayout->addStretch(1);
        rowLayout->addWidget(btnRow);
        layout_->addWidget(row);

        if (i + 1 < drafts.size()) {
            auto* sep = new QFrame(this);
            sep->setFrameShape(QFrame::HLine);
            sep->setFrameShadow(QFrame::Sunken);
            layout_->addWidget(sep);
        }
    }
    setVisible(true);
}

void ResumeBanner::clearRows() {
    QLayoutItem* item;
    while ((item = layout_->takeAt(0)) != nullptr) {
        if (auto* w = item->widget()) w->deleteLater();
        delete item;
    }
}

void ResumeBanner::confirmAndEmitDiscard(const mondoc::FillSessionId& sessionId) {
    QMessageBox box(this);
    box.setIcon(QMessageBox::Question);
    box.setWindowTitle(tr("Discard Draft"));
    box.setText(tr("Discard this draft?"));
    auto* discardBtn = box.addButton(tr("Discard Draft"),
                                      QMessageBox::DestructiveRole);
    auto* keepBtn = box.addButton(tr("Keep Editing"), QMessageBox::RejectRole);
    box.setDefaultButton(keepBtn);
    box.exec();
    if (box.clickedButton() == discardBtn) {
        emit discardRequested(sessionId);
    }
}

}  // namespace mondoc::ui
