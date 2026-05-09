#include "source_doc_pane.hpp"

#include <QFont>
#include <QLabel>
#include <QPlainTextEdit>
#include <QStackedWidget>
#include <QString>
#include <QTabWidget>
#include <QVBoxLayout>

namespace mondoc::ui {

namespace {
constexpr int kEmptyIndex  = 0;
constexpr int kSingleIndex = 1;
constexpr int kTabsIndex   = 2;
}  // namespace

SourceDocPane::SourceDocPane(QWidget* parent)
    : QWidget(parent),
      stack_(new QStackedWidget(this)),
      emptyLabel_(nullptr),
      singleView_(new QPlainTextEdit(this)),
      tabs_(new QTabWidget(this)) {
    auto* emptyContainer = new QWidget(this);
    auto* emptyLayout = new QVBoxLayout(emptyContainer);
    emptyLayout->addStretch(1);
    auto* heading = new QLabel(tr("No source documents"), emptyContainer);
    QFont hfont = heading->font();
    hfont.setBold(true);
    hfont.setPointSize(hfont.pointSize() + 2);
    heading->setFont(hfont);
    heading->setAlignment(Qt::AlignCenter);
    emptyLabel_ = heading;
    auto* body = new QLabel(
        tr("Fill in the fields on the right. You can add source documents when "
           "starting a new session."),
        emptyContainer);
    body->setAlignment(Qt::AlignCenter);
    body->setWordWrap(true);
    emptyLayout->addWidget(heading);
    emptyLayout->addWidget(body);
    emptyLayout->addStretch(2);

    singleView_->setReadOnly(true);
    singleView_->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    singleView_->setAccessibleName(tr("Source document"));

    tabs_->setTabBarAutoHide(true);
    tabs_->setMovable(false);
    tabs_->setTabsClosable(false);
    tabs_->setAccessibleName(tr("Source documents"));

    stack_->addWidget(emptyContainer);
    stack_->addWidget(singleView_);
    stack_->addWidget(tabs_);
    stack_->setCurrentIndex(kEmptyIndex);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->addWidget(stack_);

    setMinimumWidth(200);
}

void SourceDocPane::setSourceTexts(
    const std::vector<std::pair<QString, QString>>& titledBodies) {
    if (titledBodies.empty()) {
        stack_->setCurrentIndex(kEmptyIndex);
        return;
    }
    if (titledBodies.size() == 1) {
        singleView_->setPlainText(titledBodies[0].second);
        stack_->setCurrentIndex(kSingleIndex);
        return;
    }
    while (tabs_->count() > 0) {
        QWidget* w = tabs_->widget(0);
        tabs_->removeTab(0);
        delete w;
    }
    for (const auto& [title, body] : titledBodies) {
        auto* view = new QPlainTextEdit(tabs_);
        view->setReadOnly(true);
        view->setLineWrapMode(QPlainTextEdit::WidgetWidth);
        view->setPlainText(body);
        tabs_->addTab(view, title);
    }
    stack_->setCurrentIndex(kTabsIndex);
}

}  // namespace mondoc::ui
