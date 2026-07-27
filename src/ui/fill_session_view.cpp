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
      templateRepo_(templateRepo),
      undoStack_(new QUndoStack(this)) {
    undoStack_->setUndoLimit(100);

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

    auto* undoAct = undoStack_->createUndoAction(this, tr("Undo"));
    undoAct->setShortcut(QKeySequence::Undo);
    undoAct->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    addAction(undoAct);

    auto* redoAct = undoStack_->createRedoAction(this, tr("Redo"));
    redoAct->setShortcut(QKeySequence::Redo);
    redoAct->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    addAction(redoAct);
}

void FillSessionView::buildToolbar(QWidget* host) {
    auto* layout = new QHBoxLayout(host);
    layout->setContentsMargins(8, 4, 8, 4);
    layout->setSpacing(8);

    backBtn_ = new QPushButton(tr("Back to Library"), host);
    backBtn_->setShortcut(QKeySequence(Qt::Key_Escape));
    backBtn_->setAccessibleName(tr("Back to Library"));

    templateNameLabel_ = new QLabel(host);
    QFont nf = templateNameLabel_->font();
    nf.setBold(true);
    templateNameLabel_->setFont(nf);

    aiStatusLabel_ = new QLabel(host);
    aiStatusLabel_->setVisible(false);
    aiStatusLabel_->setStyleSheet(QStringLiteral("color: gray; padding-right: 8px;"));
    aiStatusLabel_->setAccessibleName(tr("AI pipeline status"));

    aiToggle_ = new QCheckBox(tr("AI"), host);
    aiToggle_->setChecked(true);
    aiToggle_->setAccessibleName(tr("AI filling enabled"));

    fillWithAiBtn_ = new QPushButton(tr("Fill with AI"), host);
    fillWithAiBtn_->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+F")));
    fillWithAiBtn_->setAccessibleName(tr("Fill with AI"));
    fillWithAiBtn_->setStyleSheet(accentButtonStyle());

    saveDraftBtn_ = new QPushButton(tr("Save Draft"), host);
    saveDraftBtn_->setShortcut(QKeySequence(QStringLiteral("Ctrl+S")));
    saveDraftBtn_->setAccessibleName(tr("Save Draft"));
    saveDraftBtn_->setToolTip(tr("Changes save automatically as you edit."));
    saveDraftBtn_->setStyleSheet(accentButtonStyle());

    exportBtn_ = new QPushButton(tr("Export\xe2\x80\xa6"), host);
    exportBtn_->setShortcut(QKeySequence(QStringLiteral("Ctrl+E")));
    exportBtn_->setAccessibleName(tr("Export Document"));
    exportBtn_->setStyleSheet(accentButtonStyle());

    layout->addWidget(backBtn_);
    layout->addWidget(templateNameLabel_, 1);
    layout->addWidget(aiStatusLabel_);
    layout->addWidget(aiToggle_);
    layout->addWidget(fillWithAiBtn_);
    layout->addWidget(saveDraftBtn_);
    layout->addWidget(exportBtn_);

    connect(backBtn_, &QPushButton::clicked, this, &FillSessionView::onBackClicked);
    connect(saveDraftBtn_, &QPushButton::clicked,
            this, &FillSessionView::onSaveDraftClicked);
    connect(exportBtn_, &QPushButton::clicked,
            this, &FillSessionView::onExportClicked);
    connect(aiToggle_, &QCheckBox::stateChanged,
            this, &FillSessionView::onAiToggleChanged);
    connect(fillWithAiBtn_, &QPushButton::clicked,
            this, &FillSessionView::onFillWithAiClicked);

    updateAiControlsVisibility();
}

void FillSessionView::buildSplitter(QWidget* host) {
    auto* layout = new QVBoxLayout(host);
    layout->setContentsMargins(0, 0, 0, 0);

    splitter_ = new QSplitter(Qt::Horizontal, host);
    splitter_->setHandleWidth(6);
    splitter_->setChildrenCollapsible(false);

    sourcePane_ = new SourceDocPane(splitter_);
    buildRightPane(splitter_);

    splitter_->addWidget(sourcePane_);
    splitter_->addWidget(rightPane_);
    splitter_->setSizes({400, 600});

    connect(fieldPane_, &FieldFormPane::sourceRefRequested,
            sourcePane_, &SourceDocPane::highlightRef);
    connect(chatPane_, &ChatPane::refinementRequested,
            this, &FillSessionView::onChatRefinementRequested);

    layout->addWidget(splitter_);
}

void FillSessionView::buildRightPane(QWidget* host) {
    rightPane_ = new QWidget(host);
    auto* layout = new QVBoxLayout(rightPane_);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    fieldPane_ = new FieldFormPane(rightPane_);
    chatPane_  = new ChatPane(rightPane_);

    layout->addWidget(fieldPane_, 1);
    layout->addWidget(chatPane_, 0);

    updateAiControlsVisibility();
}

void FillSessionView::updateAiControlsVisibility() {
    const bool configured = service_.isAiConfigured();
    aiToggle_->setVisible(configured);
    if (!configured) {
        fillWithAiBtn_->setVisible(false);
        if (chatPane_) chatPane_->setVisible(false);
        return;
    }
    const bool aiOn = aiToggle_->isChecked();
    fillWithAiBtn_->setVisible(aiOn);
    if (chatPane_) chatPane_->setVisible(aiOn);
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

    auto tplRes = templateRepo_.findById(session.template_id_);
    if (!tplRes) {
        if (errorOut) *errorOut = QString::fromStdString(tplRes.error().message());
        return false;
    }
    auto& tpl = *tplRes;

    ++sessionGeneration_;
    currentSessionId_ = id;
    currentTemplateId_ = session.template_id_;
    templateNameLabel_->setText(QString::fromStdString(tpl.name_));

    undoStack_->clear();
    fieldPane_->populate(tpl, session, service_, undoStack_, currentSessionId_);

    sourceDocIds_.clear();
    sourceTexts_.clear();
    sourceTitles_.clear();
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
        sourceDocIds_.push_back(docId);
        sourceTitles_.emplace_back(docId, title);
        sourceTexts_.emplace_back(docId, body);
        tuples.emplace_back(docId, title, body);
    }
    sourcePane_->setSourceTexts(tuples);
    return true;
}

void FillSessionView::shutdownThread(QThread*& t, AiFillWorker*& worker, bool mustJoin) {
    QThread* thread = t;
    if (!thread) return;

    // Sever delivery to `this` before anything else, on every path: this kills
    // the finished->lambda that nulls aiThread_/aiWorker_ (which could
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
    shutdownThread(aiThread_, aiWorker_, /*mustJoin=*/true);
    shutdownThread(refineThread_, refineWorker_, /*mustJoin=*/true);
}

void FillSessionView::clearSession() {
    ++sessionGeneration_;
    shutdownThread(aiThread_, aiWorker_, /*mustJoin=*/false);
    shutdownThread(refineThread_, refineWorker_, /*mustJoin=*/false);

    preFillSnapshot_.clear();
    sourceDocIds_.clear();
    sourceTexts_.clear();
    sourceTitles_.clear();

    undoStack_->clear();
    fieldPane_->clear();
    sourcePane_->setSourceTexts(std::vector<std::pair<QString, QString>>{});
    templateNameLabel_->clear();
    if (chatPane_) chatPane_->clearHistory();
    currentSessionId_ = mondoc::FillSessionId{};
    currentTemplateId_ = mondoc::TemplateId{};
}

void FillSessionView::capturePreFillSnapshot() {
    preFillSnapshot_.clear();
    auto resumed = service_.resumeSession(currentSessionId_);
    if (!resumed) return;
    for (const auto& fill : resumed->fills_) {
        preFillSnapshot_.insert(
            QString::fromStdString(fill.field_id_.value()),
            QString::fromStdString(fill.current_value_));
    }
}

void FillSessionView::restorePreFillSnapshot() {
    undoStack_->clear();
    auto resumed = service_.resumeSession(currentSessionId_);
    if (!resumed) return;
    auto tpl = templateRepo_.findById(currentTemplateId_);
    if (!tpl) return;
    fieldPane_->populate(*tpl, *resumed, service_, undoStack_, currentSessionId_);
    preFillSnapshot_.clear();
}

std::vector<mondoc::services::AiFillSourceInput>
FillSessionView::currentSources() const {
    std::vector<mondoc::services::AiFillSourceInput> out;
    out.reserve(sourceTexts_.size());
    for (std::size_t i = 0; i < sourceTexts_.size(); ++i) {
        mondoc::services::AiFillSourceInput in;
        in.id_    = sourceTexts_[i].first;
        in.title_ = sourceTitles_[i].second.toStdString();
        in.text_  = sourceTexts_[i].second.toStdString();
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
    if (aiThread_ != nullptr) {
        if (aiWorker_) aiWorker_->requestCancel();
        return;
    }
    if (!service_.isAiConfigured()) return;

    capturePreFillSnapshot();

    const QString freeFormQ = chatPane_ ? chatPane_->currentInputText() : QString();
    const std::string freeForm = freeFormQ.toStdString();
    if (!freeFormQ.trimmed().isEmpty() && chatPane_) {
        chatPane_->appendSystemMessage(
            tr("Fill with AI ran with chat input as additional context."));
        chatPane_->clearInput();
    }

    aiFillGeneration_ = sessionGeneration_;
    aiThread_ = new QThread(this);
    aiWorker_ = new AiFillWorker(service_, currentSessionId_, currentSources(), freeForm);
    aiWorker_->moveToThread(aiThread_);

    connect(aiThread_, &QThread::started, aiWorker_, &AiFillWorker::run);
    connect(aiWorker_, &AiFillWorker::finished,
            this, &FillSessionView::onAiFinished, Qt::QueuedConnection);
    connect(aiWorker_, &AiFillWorker::failed,
            this, &FillSessionView::onAiFailed, Qt::QueuedConnection);
    connect(aiWorker_, &AiFillWorker::cancelled,
            this, &FillSessionView::onAiCancelled, Qt::QueuedConnection);
    connect(aiWorker_, &AiFillWorker::finished,  aiThread_, &QThread::quit);
    connect(aiWorker_, &AiFillWorker::failed,    aiThread_, &QThread::quit);
    connect(aiWorker_, &AiFillWorker::cancelled, aiThread_, &QThread::quit);
    connect(aiThread_, &QThread::finished, aiWorker_, &QObject::deleteLater);
    connect(aiThread_, &QThread::finished, aiThread_, &QObject::deleteLater);
    connect(aiThread_, &QThread::finished, this, [this]() {
        aiThread_ = nullptr;
        aiWorker_ = nullptr;
    });

    fillWithAiBtn_->setText(tr("Cancel AI fill"));
    aiStatusLabel_->setText(tr("Filling with AI\xe2\x80\xa6"));
    aiStatusLabel_->setVisible(true);
    emit statusMessageRequested(tr("Filling with AI\xe2\x80\xa6"), 0);

    if (chatPane_) chatPane_->setBusy(true);

    aiThread_->start();
}

void FillSessionView::onAiFinished(std::vector<mondoc::domain::Fill> fills) {
    // Task 5 severs signal delivery from an abandoned fill thread on
    // shutdown/clearSession, but a fill started just before a new session
    // is opened can still race: the thread keeps running (detached, not
    // joined) and its result lands here after openSession() has already
    // pointed this view at a different session. Drop it if the generation
    // that started it no longer matches.
    if (aiFillGeneration_ != sessionGeneration_) return;

    fieldPane_->populateAi(fills);

    std::vector<mondoc::services::AiExtractedFact> facts;
    for (const auto& f : fills) {
        for (const auto& ref : f.source_refs_) {
            int sourceIndex = 0;
            for (std::size_t i = 0; i < sourceDocIds_.size(); ++i) {
                if (sourceDocIds_[i] == ref.source_id_) {
                    sourceIndex = static_cast<int>(i);
                    break;
                }
            }
            mondoc::services::AiExtractedFact e;
            e.source_index_ = sourceIndex;
            e.char_start_   = ref.range_.begin_;
            e.char_end_     = ref.range_.end_;
            e.excerpt_      = ref.excerpt_;
            e.summary_      = f.current_value_;
            facts.push_back(std::move(e));
        }
    }

    if (chatPane_) {
        chatPane_->setLastPass1Facts(std::move(facts));
        chatPane_->appendSystemMessage(tr("Fill with AI ran."));
        chatPane_->setBusy(false);
    }

    fillWithAiBtn_->setText(tr("Fill with AI"));
    aiStatusLabel_->setVisible(false);
    preFillSnapshot_.clear();
    emit statusMessageRequested(QString(), 0);
}

void FillSessionView::onAiFailed(QString message, int errorKind) {
    if (aiFillGeneration_ != sessionGeneration_) return;
    restorePreFillSnapshot();
    fillWithAiBtn_->setText(tr("Fill with AI"));
    aiStatusLabel_->setVisible(false);
    if (chatPane_) chatPane_->setBusy(false);
    emit statusMessageRequested(tr("AI fill failed."), 4000);

    showAiErrorDialog(
        mondoc::Error{static_cast<mondoc::Error::Kind>(errorKind), message.toStdString()});
}

void FillSessionView::onAiCancelled() {
    if (aiFillGeneration_ != sessionGeneration_) return;
    restorePreFillSnapshot();
    fillWithAiBtn_->setText(tr("Fill with AI"));
    aiStatusLabel_->setVisible(false);
    if (chatPane_) chatPane_->setBusy(false);
    emit statusMessageRequested(tr("AI fill cancelled."), 4000);
}

void FillSessionView::onChatRefinementRequested(
        QString prompt, std::vector<mondoc::services::AiExtractedFact> lastFacts) {
    if (!service_.isAiConfigured()) {
        if (chatPane_) {
            chatPane_->appendSystemMessage(tr("AI not configured."));
            chatPane_->setBusy(false);
        }
        return;
    }
    if (refineThread_ != nullptr) {
        if (chatPane_) chatPane_->setBusy(false);
        return;
    }

    refineGeneration_ = sessionGeneration_;
    refineThread_ = new QThread(this);
    refineWorker_ = new AiRefineWorker(service_, currentSessionId_,
                                       prompt.toStdString(), currentSources(),
                                       std::move(lastFacts));
    refineWorker_->moveToThread(refineThread_);

    connect(refineThread_, &QThread::started, refineWorker_, &AiRefineWorker::run);
    connect(refineWorker_, &AiRefineWorker::finished,
            this, &FillSessionView::onChatRefineFinished, Qt::QueuedConnection);
    connect(refineWorker_, &AiRefineWorker::failed,
            this, &FillSessionView::onChatRefineFailed, Qt::QueuedConnection);
    connect(refineWorker_, &AiRefineWorker::finished, refineThread_, &QThread::quit);
    connect(refineWorker_, &AiRefineWorker::failed,   refineThread_, &QThread::quit);
    connect(refineWorker_, &AiRefineWorker::cancelled, refineThread_, &QThread::quit);
    connect(refineThread_, &QThread::finished, refineWorker_, &QObject::deleteLater);
    connect(refineThread_, &QThread::finished, refineThread_, &QObject::deleteLater);
    connect(refineThread_, &QThread::finished, this, [this]() {
        refineThread_ = nullptr;
        refineWorker_ = nullptr;
    });

    refineThread_->start();
}

void FillSessionView::onChatRefineFinished(std::vector<mondoc::domain::Fill> fills) {
    // Task 5 severs signal delivery from an abandoned refine thread on
    // shutdown/clearSession, but a refine started just before a new session
    // is opened can still race: the thread keeps running (detached, not
    // joined) and its result lands here after openSession() has already
    // pointed this view at a different session. Drop it if the generation
    // that started it no longer matches.
    if (refineGeneration_ != sessionGeneration_) return;
    if (!fills.empty()) {
        fieldPane_->populateAi(fills);
        if (chatPane_)
            chatPane_->appendAiMessage(tr("Updated %1 field(s).").arg(fills.size()));
    } else if (chatPane_) {
        chatPane_->appendAiMessage(tr("No changes applied."));
    }
    if (chatPane_) chatPane_->setBusy(false);
}

void FillSessionView::onChatRefineFailed(QString message) {
    if (refineGeneration_ != sessionGeneration_) return;
    if (chatPane_) {
        chatPane_->appendSystemMessage(tr("Refinement failed: %1").arg(message));
        chatPane_->setBusy(false);
    }
    emit statusMessageRequested(tr("AI fill failed."), 4000);
}

void FillSessionView::onBackClicked() {
    if (!currentSessionId_.value().empty()) {
        QMessageBox box(this);
        box.setIcon(QMessageBox::Warning);
        box.setWindowTitle(tr("Discard Session"));
        box.setText(
            tr("Discard the fill session for \xe2\x80\x9c%1\xe2\x80\x9d? "
               "Unsaved changes will be lost.")
                .arg(templateNameLabel_->text()));
        auto* discardBtn = box.addButton(tr("Discard Session"),
                                         QMessageBox::DestructiveRole);
        auto* keepBtn = box.addButton(tr("Keep Editing"), QMessageBox::RejectRole);
        box.setDefaultButton(keepBtn);
        box.exec();
        if (box.clickedButton() != discardBtn) return;
        if (auto discardResult = service_.discardSession(currentSessionId_); !discardResult) {
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
    auto res = service_.exportSession(currentSessionId_, dlg.selectedFormat(), path);
    if (res) {
        emit sessionExported(QFileInfo(pathToQString(path)).fileName());
    } else {
        emit exportFailed(QString::fromStdString(res.error().message()));
    }
}

}  // namespace mondoc::ui
