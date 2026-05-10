#include "fill_session_view.hpp"

#include <QAction>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QString>
#include <QStringLiteral>
#include <QUndoStack>
#include <QVBoxLayout>

#include "export_dialog.hpp"
#include "field_form_pane.hpp"
#include "source_doc_pane.hpp"

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
      backBtn_(nullptr),
      templateNameLabel_(nullptr),
      saveDraftBtn_(nullptr),
      exportBtn_(nullptr),
      splitter_(nullptr),
      sourcePane_(nullptr),
      fieldPane_(nullptr),
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

    saveDraftBtn_ = new QPushButton(tr("Save Draft"), host);
    saveDraftBtn_->setShortcut(QKeySequence(QStringLiteral("Ctrl+S")));
    saveDraftBtn_->setAccessibleName(tr("Save Draft"));
    saveDraftBtn_->setStyleSheet(
        QStringLiteral("QPushButton { background-color: #2563EB; color: white; "
                       "padding: 6px 12px; }"));

    exportBtn_ = new QPushButton(tr("Export\xe2\x80\xa6"), host);
    exportBtn_->setShortcut(QKeySequence(QStringLiteral("Ctrl+E")));
    exportBtn_->setAccessibleName(tr("Export Document"));
    exportBtn_->setStyleSheet(
        QStringLiteral("QPushButton { background-color: #2563EB; color: white; "
                       "padding: 6px 12px; }"));

    layout->addWidget(backBtn_);
    layout->addWidget(templateNameLabel_, 1);
    layout->addWidget(saveDraftBtn_);
    layout->addWidget(exportBtn_);

    connect(backBtn_, &QPushButton::clicked, this, &FillSessionView::onBackClicked);
    connect(saveDraftBtn_, &QPushButton::clicked,
            this, &FillSessionView::onSaveDraftClicked);
    connect(exportBtn_, &QPushButton::clicked,
            this, &FillSessionView::onExportClicked);
}

void FillSessionView::buildSplitter(QWidget* host) {
    auto* layout = new QVBoxLayout(host);
    layout->setContentsMargins(0, 0, 0, 0);

    splitter_ = new QSplitter(Qt::Horizontal, host);
    splitter_->setHandleWidth(6);
    splitter_->setChildrenCollapsible(false);

    sourcePane_ = new SourceDocPane(splitter_);
    fieldPane_ = new FieldFormPane(splitter_);

    splitter_->addWidget(sourcePane_);
    splitter_->addWidget(fieldPane_);
    splitter_->setSizes({400, 600});

    layout->addWidget(splitter_);
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

    currentSessionId_ = id;
    currentTemplateId_ = session.template_id_;
    templateNameLabel_->setText(QString::fromStdString(tpl.name_));

    undoStack_->clear();
    fieldPane_->populate(tpl, session, service_, undoStack_, currentSessionId_);

    std::vector<std::pair<QString, QString>> sources;
    sources.reserve(sourcePaths.size());
    for (const auto& path : sourcePaths) {
        const QString title = QFileInfo(pathToQString(path)).fileName();
        auto textRes = service_.readSourceText(path);
        if (textRes) {
            sources.emplace_back(title, QString::fromStdString(*textRes));
        } else {
            sources.emplace_back(
                title,
                tr("Cannot read this source: %1")
                    .arg(QString::fromStdString(textRes.error().message())));
        }
    }
    sourcePane_->setSourceTexts(sources);
    return true;
}

void FillSessionView::clearSession() {
    undoStack_->clear();
    fieldPane_->clear();
    sourcePane_->setSourceTexts({});
    templateNameLabel_->clear();
    currentSessionId_ = mondoc::FillSessionId{};
    currentTemplateId_ = mondoc::TemplateId{};
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
        (void)service_.discardSession(currentSessionId_);
    }
    emit backRequested();
}

void FillSessionView::onSaveDraftClicked() { emit draftSaved(); }

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
