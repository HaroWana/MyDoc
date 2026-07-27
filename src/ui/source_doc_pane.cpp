#include "source_doc_pane.hpp"

#include <QByteArray>
#include <QFont>
#include <QLabel>
#include <QPlainTextEdit>
#include <QStackedWidget>
#include <QString>
#include <QTabWidget>
#include <QTextCursor>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QtGlobal>

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

void SourceDocPane::setSourceTexts(
        const std::vector<std::tuple<mondoc::SourceDocId, QString, QString>>& sources) {
    std::vector<std::pair<QString, QString>> legacy;
    legacy.reserve(sources.size());
    for (const auto& [id, title, body] : sources) {
        legacy.emplace_back(title, body);
    }
    setSourceTexts(legacy);

    viewByDocId_.clear();
    if (sources.size() == 1) {
        viewByDocId_[std::get<0>(sources[0]).value()] = singleView_;
    } else {
        for (int i = 0; i < tabs_->count() && i < static_cast<int>(sources.size()); ++i) {
            auto* view = qobject_cast<QPlainTextEdit*>(tabs_->widget(i));
            if (view) {
                viewByDocId_[std::get<0>(sources[i]).value()] = view;
            }
        }
    }
}

void SourceDocPane::highlightRef(const mondoc::domain::SourceRef& ref) {
    auto it = viewByDocId_.find(ref.source_id_.value());
    if (it == viewByDocId_.end() || !it->second) {
        qWarning("SourceDocPane::highlightRef: unknown source id %s",
                 ref.source_id_.value().c_str());
        if (singleView_) singleView_->setExtraSelections({});
        return;
    }
    QPlainTextEdit* view = it->second;

    if (tabs_ && stack_->currentWidget() == tabs_) {
        for (int i = 0; i < tabs_->count(); ++i) {
            if (tabs_->widget(i) == view) {
                tabs_->setCurrentIndex(i);
                break;
            }
        }
    }

    // ref.range_ holds UTF-8 byte offsets, but QTextCursor::setPosition takes
    // UTF-16 code unit offsets, so re-decode the UTF-8 prefix up to each
    // offset and count its UTF-16 length.
    const QByteArray utf8 = view->toPlainText().toUtf8();
    const int start = QString::fromUtf8(utf8.data(), static_cast<int>(ref.range_.begin_)).size();
    const int end   = QString::fromUtf8(utf8.data(), static_cast<int>(ref.range_.end_)).size();
    QTextCursor cur(view->document());
    cur.setPosition(start);
    cur.setPosition(end, QTextCursor::KeepAnchor);

    QTextEdit::ExtraSelection sel;
    sel.cursor = cur;
    sel.format.setBackground(QColor(QStringLiteral("#FEF08A")));
    view->setExtraSelections({sel});

    view->setTextCursor(cur);
    view->ensureCursorVisible();
}

}  // namespace mondoc::ui
