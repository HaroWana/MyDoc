#include "fill_session_view.hpp"

#include "mondoc/util.hpp"

#include <QAction>
#include <QCheckBox>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QString>
#include <QStringLiteral>
#include <QThread>
#include <QUndoStack>
#include <QVBoxLayout>
#include <QtGlobal>

#include "ai_fill_worker.hpp"
#include "ai_refine_worker.hpp"
#include "chat_pane.hpp"
#include "export_dialog.hpp"
#include "field_form_pane.hpp"
#include "llm_error_text.hpp"
#include "source_doc_pane.hpp"
#include "ui_style.hpp"

namespace mondoc::ui {

namespace {

QString pathToQString(const std::filesystem::path& p) {
    return QString::fromStdU16String(p.u16string());
}

}  // namespace

FillSessionView::FillSessionView(mondoc::services::FillSessionService& service,
                                 mondoc::domain::ITemplateRepository& templateRepo,
                                 QWidget* parent)
    : QWidget(parent),
      service_(service),
      template_repo_(templateRepo),
      undo_stack_(new QUndoStack(this)) {
    undo_stack_->setUndoLimit(100);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto* toolbar = new QWidget(this);
    toolbar->setFixedHeight(44);
    buildToolbar(toolbar);
    root->addWidget(toolbar);

    auto* body = new QWidget(this);
    buildSplitter(body);
    root->addWidget(body, 1);

    auto* undoAct = undo_stack_->createUndoAction(this, tr("Undo"));
    undoAct->setShortcut(QKeySequence::Undo);
    undoAct->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    addAction(undoAct);

    auto* redoAct = undo_stack_->createRedoAction(this, tr("Redo"));
    redoAct->setShortcut(QKeySequence::Redo);
    redoAct->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    addAction(redoAct);
}

void FillSessionView::buildToolbar(QWidget* host) {
    auto* layout = new QHBoxLayout(host);
    layout->setContentsMargins(8, 4, 8, 4);
    layout->setSpacing(8);

    back_btn_ = new QPushButton(tr("Back to Library"), host);
    back_btn_->setShortcut(QKeySequence(Qt::Key_Escape));
    back_btn_->setAccessibleName(tr("Back to Library"));

    template_name_label_ = new QLabel(host);
    QFont nf = template_name_label_->font();
    nf.setBold(true);
    template_name_label_->setFont(nf);

    ai_status_label_ = new QLabel(host);
    ai_status_label_->setVisible(false);
    ai_status_label_->setStyleSheet(QStringLiteral("color: gray; padding-right: 8px;"));
    ai_status_label_->setAccessibleName(tr("AI pipeline status"));

    ai_toggle_ = new QCheckBox(tr("AI"), host);
    ai_toggle_->setChecked(true);
    ai_toggle_->setAccessibleName(tr("AI filling enabled"));

    fill_with_ai_btn_ = new QPushButton(tr("Fill with AI"), host);
    fill_with_ai_btn_->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+F")));
    fill_with_ai_btn_->setAccessibleName(tr("Fill with AI"));
    fill_with_ai_btn_->setStyleSheet(accentButtonStyle());

    save_draft_btn_ = new QPushButton(tr("Save Draft"), host);
    save_draft_btn_->setShortcut(QKeySequence(QStringLiteral("Ctrl+S")));
    save_draft_btn_->setAccessibleName(tr("Save Draft"));
    save_draft_btn_->setToolTip(tr("Changes save automatically as you edit."));
    save_draft_btn_->setStyleSheet(accentButtonStyle());

    export_btn_ = new QPushButton(tr("Export\xe2\x80\xa6"), host);
    export_btn_->setShortcut(QKeySequence(QStringLiteral("Ctrl+E")));
    export_btn_->setAccessibleName(tr("Export Document"));
    export_btn_->setStyleSheet(accentButtonStyle());

    layout->addWidget(back_btn_);
    layout->addWidget(template_name_label_, 1);
    layout->addWidget(ai_status_label_);
    layout->addWidget(ai_toggle_);
    layout->addWidget(fill_with_ai_btn_);
    layout->addWidget(save_draft_btn_);
    layout->addWidget(export_btn_);

    connect(back_btn_, &QPushButton::clicked, this, &FillSessionView::onBackClicked);
    connect(save_draft_btn_, &QPushButton::clicked,
            this, &FillSessionView::onSaveDraftClicked);
    connect(export_btn_, &QPushButton::clicked,
            this, &FillSessionView::onExportClicked);
    connect(ai_toggle_, &QCheckBox::stateChanged,
            this, &FillSessionView::onAiToggleChanged);
    connect(fill_with_ai_btn_, &QPushButton::clicked,
            this, &FillSessionView::onFillWithAiClicked);

    updateAiControlsVisibility();
}

void FillSessionView::buildSplitter(QWidget* host) {
    auto* layout = new QVBoxLayout(host);
    layout->setContentsMargins(0, 0, 0, 0);

    splitter_ = new QSplitter(Qt::Horizontal, host);
    splitter_->setHandleWidth(6);
    splitter_->setChildrenCollapsible(false);

    source_pane_ = new SourceDocPane(splitter_);
    buildRightPane(splitter_);

    splitter_->addWidget(source_pane_);
    splitter_->addWidget(right_pane_);
    splitter_->setSizes({400, 600});

    connect(field_pane_, &FieldFormPane::sourceRefRequested,
            source_pane_, &SourceDocPane::highlightRef);
    connect(chat_pane_, &ChatPane::refinementRequested,
            this, &FillSessionView::onChatRefinementRequested);

    layout->addWidget(splitter_);
}

void FillSessionView::buildRightPane(QWidget* host) {
    right_pane_ = new QWidget(host);
    auto* layout = new QVBoxLayout(right_pane_);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    field_pane_ = new FieldFormPane(right_pane_);
    chat_pane_  = new ChatPane(right_pane_);

    layout->addWidget(field_pane_, 1);
    layout->addWidget(chat_pane_, 0);

    updateAiControlsVisibility();
}

void FillSessionView::updateAiControlsVisibility() {
    const bool configured = service_.isAiConfigured();
    ai_toggle_->setVisible(configured);
    if (!configured) {
        fill_with_ai_btn_->setVisible(false);
        if (chat_pane_) chat_pane_->setVisible(false);
        return;
    }
    const bool aiOn = ai_toggle_->isChecked();
    fill_with_ai_btn_->setVisible(aiOn);
    if (chat_pane_) chat_pane_->setVisible(aiOn);
}

bool FillSessionView::openSession(const mondoc::FillSessionId& id,
                                  const std::vector<std::filesystem::path>& sourcePaths,
                                  QString* errorOut) {
    auto sessionRes = service_.resumeSession(id);
    if (!sessionRes) {
        if (errorOut) *errorOut = QString::fromStdString(sessionRes.error().message());
        return false;
    }
    auto& session = *sessionRes;

    auto tplRes = template_repo_.findById(session.template_id_);
    if (!tplRes) {
        if (errorOut) *errorOut = QString::fromStdString(tplRes.error().message());
        return false;
    }
    auto& tpl = *tplRes;

    ++session_generation_;
    current_session_id_ = id;
    current_template_id_ = session.template_id_;
    template_name_label_->setText(QString::fromStdString(tpl.name_));

    undo_stack_->clear();
    field_pane_->populate(tpl, session, service_, undo_stack_, current_session_id_);

    source_doc_ids_.clear();
    source_texts_.clear();
    source_titles_.clear();
    std::vector<std::tuple<mondoc::SourceDocId, QString, QString>> tuples;
    tuples.reserve(sourcePaths.size());
    for (const auto& path : sourcePaths) {
        const QString title = QFileInfo(pathToQString(path)).fileName();
        auto textRes = service_.readSourceText(path);
        const QString body = textRes
            ? QString::fromStdString(*textRes)
            : tr("Cannot read this source: %1")
                .arg(QString::fromStdString(textRes.error().message()));
        auto docId = mondoc::SourceDocId(generateUuid());
        source_doc_ids_.push_back(docId);
        source_titles_.emplace_back(docId, title);
        source_texts_.emplace_back(docId, body);
        tuples.emplace_back(docId, title, body);
    }
    source_pane_->setSourceTexts(tuples);
    return true;
}

void FillSessionView::shutdownThread(QThread*& t, AiFillWorker*& worker, bool mustJoin) {
    QThread* thread = t;
    if (!thread) return;

    // Sever delivery to `this` before anything else, on every path: this kills
    // the finished->lambda that nulls ai_thread_/ai_worker_ (which could
    // otherwise fire late and stomp a subsequently-started thread's state)
    // and stops the worker's own result signals (finished/failed/cancelled ->
    // onAiFinished/onAiFailed/onAiCancelled) from reaching this view once
    // shutdown has begun. Connections from the worker to the thread (e.g.
    // finished -> thread->quit) are left intact so an abandoned thread still
    // exits and its finished->deleteLater cleanup still runs.
    if (worker) worker->disconnect(this);
    thread->disconnect(this);
    if (worker) worker->requestCancel();
    t = nullptr;
    worker = nullptr;

    if (!thread->isRunning()) return;

    thread->quit();
    if (thread->wait(5000)) return;

    if (mustJoin) {
        // Called from ~FillSessionView(): main.cpp destroys the
        // CompositionRoot (and with it FillSessionService/the repositories)
        // within a few lines of destroying MainWindow, with no event-loop
        // turn in between. Abandoning here would let this worker's
        // non-interruptible HTTP read (up to 60s, llm_client's read timeout)
        // keep running into already-freed services. Block until the OS
        // thread actually exits instead — this is the price of
        // requestCancel() being unable to interrupt an in-flight read.
        thread->wait();
        return;
    }

    // Session-clear mode: the CompositionRoot/services outlive this view for
    // the rest of the app's life, and delivery to `this` is already severed
    // above, so it is safe to detach and let the abandoned thread finish on
    // its own time. requestCancel() cannot interrupt an in-flight HTTP read,
    // so terminate() would be the expected path on every close-during-request
    // and could hang the UI for up to that same 60s (or land the OS thread
    // mid-syscall on some platforms). Detach instead: unparent so the
    // QObject child-cascade can't reach a running thread, and let the
    // existing worker->finished -> thread->quit and thread->finished ->
    // deleteLater connections reap both objects once the in-flight request
    // actually completes.
    thread->setParent(nullptr);
}

void FillSessionView::shutdownThread(QThread*& t, AiRefineWorker*& worker, bool mustJoin) {
    QThread* thread = t;
    if (!thread) return;

    // Same rationale as the AiFillWorker overload above: sever delivery to
    // `this` before anything else, then request cancellation so a worker
    // blocked on requestCancel()-honoring code can unwind promptly.
    if (worker) worker->disconnect(this);
    thread->disconnect(this);
    if (worker) worker->requestCancel();
    t = nullptr;
    worker = nullptr;

    if (!thread->isRunning()) return;

    thread->quit();
    if (thread->wait(5000)) return;

    if (mustJoin) {
        // Same CompositionRoot-lifetime rationale as the AiFillWorker
        // overload above: refineField() also touches service_, so the
        // destructor must block until the thread actually exits.
        thread->wait();
        return;
    }

    // Session-clear mode: same detach rationale as the AiFillWorker overload
    // above — requestCancel() cannot interrupt an in-flight HTTP read, so
    // detach and let the existing finished/failed/cancelled -> thread->quit
    // and thread->finished -> deleteLater connections reap both objects once
    // the in-flight request actually completes.
    thread->setParent(nullptr);
}

FillSessionView::~FillSessionView() {
    shutdownThread(ai_thread_, ai_worker_, /*mustJoin=*/true);
    shutdownThread(refine_thread_, refine_worker_, /*mustJoin=*/true);
}

void FillSessionView::clearSession() {
    ++session_generation_;
    shutdownThread(ai_thread_, ai_worker_, /*mustJoin=*/false);
    shutdownThread(refine_thread_, refine_worker_, /*mustJoin=*/false);

    pre_fill_snapshot_.clear();
    source_doc_ids_.clear();
    source_texts_.clear();
    source_titles_.clear();

    undo_stack_->clear();
    field_pane_->clear();
    source_pane_->setSourceTexts(std::vector<std::pair<QString, QString>>{});
    template_name_label_->clear();
    if (chat_pane_) chat_pane_->clearHistory();
    current_session_id_ = mondoc::FillSessionId{};
    current_template_id_ = mondoc::TemplateId{};
}

void FillSessionView::capturePreFillSnapshot() {
    pre_fill_snapshot_.clear();
    auto resumed = service_.resumeSession(current_session_id_);
    if (!resumed) return;
    for (const auto& fill : resumed->fills_) {
        pre_fill_snapshot_.insert(
            QString::fromStdString(fill.field_id_.value()),
            QString::fromStdString(fill.current_value_));
    }
}

void FillSessionView::restorePreFillSnapshot() {
    undo_stack_->clear();
    auto resumed = service_.resumeSession(current_session_id_);
    if (!resumed) return;
    auto tpl = template_repo_.findById(current_template_id_);
    if (!tpl) return;
    field_pane_->populate(*tpl, *resumed, service_, undo_stack_, current_session_id_);
    pre_fill_snapshot_.clear();
}

std::vector<mondoc::domain::AiSourceDoc>
FillSessionView::currentSources() const {
    std::vector<mondoc::domain::AiSourceDoc> out;
    out.reserve(source_texts_.size());
    for (std::size_t i = 0; i < source_texts_.size(); ++i) {
        mondoc::domain::AiSourceDoc in;
        in.id_    = source_texts_[i].first;
        in.title_ = source_titles_[i].second.toStdString();
        in.text_  = source_texts_[i].second.toStdString();
        out.push_back(std::move(in));
    }
    return out;
}

void FillSessionView::showAiErrorDialog(const mondoc::Error& error) {
    if (error.kind() == mondoc::Error::Kind::Cancelled) return;

    QString title;
    QMessageBox::Icon icon = QMessageBox::Warning;
    switch (error.kind()) {
        case mondoc::Error::Kind::Unreachable:
            title = tr("AI hub unreachable");
            break;
        case mondoc::Error::Kind::RateLimited:
            title = tr("AI hub rate-limited");
            icon  = QMessageBox::Information;
            break;
        default:
            title = tr("AI hub returned an unexpected response");
            break;
    }
    QMessageBox box(icon, title, llmErrorText(error), QMessageBox::Ok, this);
    box.button(QMessageBox::Ok)->setText(tr("Close"));
    box.exec();
}

void FillSessionView::onAiToggleChanged(int /*state*/) {
    updateAiControlsVisibility();
}

void FillSessionView::onFillWithAiClicked() {
    if (ai_thread_ != nullptr) {
        if (ai_worker_) ai_worker_->requestCancel();
        return;
    }
    if (!service_.isAiConfigured()) return;

    capturePreFillSnapshot();

    const QString freeFormQ = chat_pane_ ? chat_pane_->currentInputText() : QString();
    const std::string freeForm = freeFormQ.toStdString();
    if (!freeFormQ.trimmed().isEmpty() && chat_pane_) {
        chat_pane_->appendSystemMessage(
            tr("Fill with AI ran with chat input as additional context."));
        chat_pane_->clearInput();
    }

    ai_fill_generation_ = session_generation_;
    ai_thread_ = new QThread(this);
    ai_worker_ = new AiFillWorker(service_, current_session_id_, currentSources(), freeForm);
    ai_worker_->moveToThread(ai_thread_);

    connect(ai_thread_, &QThread::started, ai_worker_, &AiFillWorker::run);
    connect(ai_worker_, &AiFillWorker::finished,
            this, &FillSessionView::onAiFinished, Qt::QueuedConnection);
    connect(ai_worker_, &AiFillWorker::failed,
            this, &FillSessionView::onAiFailed, Qt::QueuedConnection);
    connect(ai_worker_, &AiFillWorker::cancelled,
            this, &FillSessionView::onAiCancelled, Qt::QueuedConnection);
    connect(ai_worker_, &AiFillWorker::finished,  ai_thread_, &QThread::quit);
    connect(ai_worker_, &AiFillWorker::failed,    ai_thread_, &QThread::quit);
    connect(ai_worker_, &AiFillWorker::cancelled, ai_thread_, &QThread::quit);
    connect(ai_thread_, &QThread::finished, ai_worker_, &QObject::deleteLater);
    connect(ai_thread_, &QThread::finished, ai_thread_, &QObject::deleteLater);
    connect(ai_thread_, &QThread::finished, this, [this]() {
        ai_thread_ = nullptr;
        ai_worker_ = nullptr;
    });

    fill_with_ai_btn_->setText(tr("Cancel AI fill"));
    ai_status_label_->setText(tr("Filling with AI\xe2\x80\xa6"));
    ai_status_label_->setVisible(true);
    emit statusMessageRequested(tr("Filling with AI\xe2\x80\xa6"), 0);

    if (chat_pane_) chat_pane_->setBusy(true);

    ai_thread_->start();
}

void FillSessionView::onAiFinished(std::vector<mondoc::domain::Fill> fills) {
    // Task 5 severs signal delivery from an abandoned fill thread on
    // shutdown/clearSession, but a fill started just before a new session
    // is opened can still race: the thread keeps running (detached, not
    // joined) and its result lands here after openSession() has already
    // pointed this view at a different session. Drop it if the generation
    // that started it no longer matches.
    if (ai_fill_generation_ != session_generation_) return;

    field_pane_->populateAi(fills);

    std::vector<mondoc::domain::AiExtractedFact> facts;
    for (const auto& f : fills) {
        for (const auto& ref : f.source_refs_) {
            int sourceIndex = 0;
            for (std::size_t i = 0; i < source_doc_ids_.size(); ++i) {
                if (source_doc_ids_[i] == ref.source_id_) {
                    sourceIndex = static_cast<int>(i);
                    break;
                }
            }
            mondoc::domain::AiExtractedFact e;
            e.source_index_ = sourceIndex;
            e.char_start_   = ref.range_.begin_;
            e.char_end_     = ref.range_.end_;
            e.excerpt_      = ref.excerpt_;
            e.summary_      = f.current_value_;
            facts.push_back(std::move(e));
        }
    }

    if (chat_pane_) {
        chat_pane_->setLastPass1Facts(std::move(facts));
        chat_pane_->appendSystemMessage(tr("Fill with AI ran."));
        chat_pane_->setBusy(false);
    }

    fill_with_ai_btn_->setText(tr("Fill with AI"));
    ai_status_label_->setVisible(false);
    pre_fill_snapshot_.clear();
    emit statusMessageRequested(QString(), 0);
}

void FillSessionView::onAiFailed(QString message, int errorKind) {
    if (ai_fill_generation_ != session_generation_) return;
    restorePreFillSnapshot();
    fill_with_ai_btn_->setText(tr("Fill with AI"));
    ai_status_label_->setVisible(false);
    if (chat_pane_) chat_pane_->setBusy(false);
    emit statusMessageRequested(tr("AI fill failed."), 4000);

    showAiErrorDialog(
        mondoc::Error{static_cast<mondoc::Error::Kind>(errorKind), message.toStdString()});
}

void FillSessionView::onAiCancelled() {
    if (ai_fill_generation_ != session_generation_) return;
    restorePreFillSnapshot();
    fill_with_ai_btn_->setText(tr("Fill with AI"));
    ai_status_label_->setVisible(false);
    if (chat_pane_) chat_pane_->setBusy(false);
    emit statusMessageRequested(tr("AI fill cancelled."), 4000);
}

void FillSessionView::onChatRefinementRequested(
        QString prompt, std::vector<mondoc::domain::AiExtractedFact> lastFacts) {
    if (!service_.isAiConfigured()) {
        if (chat_pane_) {
            chat_pane_->appendSystemMessage(tr("AI not configured."));
            chat_pane_->setBusy(false);
        }
        return;
    }
    if (refine_thread_ != nullptr) {
        if (chat_pane_) chat_pane_->setBusy(false);
        return;
    }

    refine_generation_ = session_generation_;
    refine_thread_ = new QThread(this);
    refine_worker_ = new AiRefineWorker(service_, current_session_id_,
                                       prompt.toStdString(), currentSources(),
                                       std::move(lastFacts));
    refine_worker_->moveToThread(refine_thread_);

    connect(refine_thread_, &QThread::started, refine_worker_, &AiRefineWorker::run);
    connect(refine_worker_, &AiRefineWorker::finished,
            this, &FillSessionView::onChatRefineFinished, Qt::QueuedConnection);
    connect(refine_worker_, &AiRefineWorker::failed,
            this, &FillSessionView::onChatRefineFailed, Qt::QueuedConnection);
    connect(refine_worker_, &AiRefineWorker::finished, refine_thread_, &QThread::quit);
    connect(refine_worker_, &AiRefineWorker::failed,   refine_thread_, &QThread::quit);
    connect(refine_worker_, &AiRefineWorker::cancelled, refine_thread_, &QThread::quit);
    connect(refine_thread_, &QThread::finished, refine_worker_, &QObject::deleteLater);
    connect(refine_thread_, &QThread::finished, refine_thread_, &QObject::deleteLater);
    connect(refine_thread_, &QThread::finished, this, [this]() {
        refine_thread_ = nullptr;
        refine_worker_ = nullptr;
    });

    refine_thread_->start();
}

void FillSessionView::onChatRefineFinished(std::vector<mondoc::domain::Fill> fills) {
    // Task 5 severs signal delivery from an abandoned refine thread on
    // shutdown/clearSession, but a refine started just before a new session
    // is opened can still race: the thread keeps running (detached, not
    // joined) and its result lands here after openSession() has already
    // pointed this view at a different session. Drop it if the generation
    // that started it no longer matches.
    if (refine_generation_ != session_generation_) return;
    if (!fills.empty()) {
        field_pane_->populateAi(fills);
        if (chat_pane_)
            chat_pane_->appendAiMessage(tr("Updated %1 field(s).").arg(fills.size()));
    } else if (chat_pane_) {
        chat_pane_->appendAiMessage(tr("No changes applied."));
    }
    if (chat_pane_) chat_pane_->setBusy(false);
}

void FillSessionView::onChatRefineFailed(QString message) {
    if (refine_generation_ != session_generation_) return;
    if (chat_pane_) {
        chat_pane_->appendSystemMessage(tr("Refinement failed: %1").arg(message));
        chat_pane_->setBusy(false);
    }
    emit statusMessageRequested(tr("AI fill failed."), 4000);
}

void FillSessionView::onBackClicked() {
    if (!current_session_id_.value().empty()) {
        QMessageBox box(this);
        box.setIcon(QMessageBox::Warning);
        box.setWindowTitle(tr("Discard Session"));
        box.setText(
            tr("Discard the fill session for \xe2\x80\x9c%1\xe2\x80\x9d? "
               "Unsaved changes will be lost.")
                .arg(template_name_label_->text()));
        auto* discardBtn = box.addButton(tr("Discard Session"),
                                         QMessageBox::DestructiveRole);
        auto* keepBtn = box.addButton(tr("Keep Editing"), QMessageBox::RejectRole);
        box.setDefaultButton(keepBtn);
        box.exec();
        if (box.clickedButton() != discardBtn) return;
        if (auto discardResult = service_.discardSession(current_session_id_); !discardResult) {
            qWarning("FillSessionView::onBackClicked: failed to discard session: %s",
                     discardResult.error().message().c_str());
        }
    }
    emit backRequested();
}

void FillSessionView::onSaveDraftClicked() {
    emit statusMessageRequested(tr("All changes save automatically"), 3000);
}

void FillSessionView::onExportClicked() {
    ExportDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted) return;

    const auto path = dlg.selectedPath();
    auto res = service_.exportSession(current_session_id_, dlg.selectedFormat(), path);
    if (res) {
        emit sessionExported(QFileInfo(pathToQString(path)).fileName());
    } else {
        emit exportFailed(QString::fromStdString(res.error().message()));
    }
}

}  // namespace mondoc::ui
