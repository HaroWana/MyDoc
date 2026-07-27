#include "main_window.hpp"

#include <QAction>
#include <QApplication>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPoint>
#include <QPushButton>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyle>
#include <QToolBar>
#include <QUrl>
#include <QVBoxLayout>
#include <QVariant>
#include <QWidget>

#include <chrono>
#include <ctime>
#include <set>
#include <string>

#include "about_dialog.hpp"
#include "fill_session_view.hpp"
#include "import_conflict_dialog.hpp"
#include "region_mark_viewer.hpp"
#include "resume_banner.hpp"
#include "schema_dock_widget.hpp"
#include "settings_dialog.hpp"

namespace mondoc::ui {

namespace {

constexpr int kTemplateIdRole = Qt::UserRole + 1;

const std::set<QString>& acceptedExtensions() {
    static const std::set<QString> exts{
        QStringLiteral("docx"),
        QStringLiteral("odt"),
        QStringLiteral("pdf"),
        QStringLiteral("txt"),
        QStringLiteral("md"),
    };
    return exts;
}

bool hasAcceptedExtension(const QUrl& url) {
    if (!url.isLocalFile()) return false;
    const QFileInfo info(url.toLocalFile());
    return acceptedExtensions().contains(info.suffix().toLower());
}

std::filesystem::path qStringToPath(const QString& s) {
    return std::filesystem::path{s.toStdU16String()};
}

QString pathToQString(const std::filesystem::path& p) {
    return QString::fromStdU16String(p.u16string());
}

}  // namespace

MainWindow::MainWindow(mondoc::services::TemplateService& templateService,
                       mondoc::services::FillSessionService& fillService,
                       mondoc::domain::ITemplateRepository& templateRepo,
                       const mondoc::adapters::ai::LlmConfig& currentConfig,
                       std::function<void(mondoc::adapters::ai::LlmConfig)> reconfigureLlmCallback,
                       QWidget* parent)
    : QMainWindow(parent),
      service_(templateService),
      fillService_(fillService),
      templateRepo_(templateRepo),
      currentConfig_(currentConfig),
      reconfigureLlmCallback_(std::move(reconfigureLlmCallback)),
      templateList_(new QListWidget(this)),
      centralStack_(new QStackedWidget(this)),
      searchBox_(new QLineEdit(this)),
      schemaWidget_(new SchemaDockWidget(this)),
      fillSessionView_(nullptr),
      resumeBanner_(nullptr),
      detailNameLabel_(nullptr),
      detailFormatLabel_(nullptr),
      detailFieldCountLabel_(nullptr),
      detailCreatedLabel_(nullptr),
      detailFillBtn_(nullptr),
      detailRenameBtn_(nullptr),
      detailDuplicateBtn_(nullptr),
      detailDeleteBtn_(nullptr) {
    setWindowTitle(tr("MonDoc"));
    setAcceptDrops(true);

    fillSessionView_ = new FillSessionView(fillService_, templateRepo_, this);
    resumeBanner_ = new ResumeBanner(this);

    auto* sidebar = new QWidget(this);
    sidebar->setMinimumWidth(180);
    buildSidebar(sidebar);

    auto* emptyPage = new QWidget(this);
    buildEmptyState(emptyPage);
    auto* detailPage = new QWidget(this);
    buildDetailPage(detailPage);
    centralStack_->addWidget(emptyPage);
    centralStack_->addWidget(detailPage);
    centralStack_->addWidget(fillSessionView_);
    centralStack_->setCurrentIndex(0);

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->addWidget(sidebar);
    splitter->addWidget(centralStack_);
    splitter->setChildrenCollapsible(false);
    splitter->setSizes({240, 800});
    setCentralWidget(splitter);

    schemaWidget_->setWindowTitle(tr("Schema"));
    addDockWidget(Qt::RightDockWidgetArea, schemaWidget_);
    schemaWidget_->hide();

    statusBar();

    // Menu bar
    auto* fileMenu = menuBar()->addMenu(tr("&File"));
    exportAction_ = fileMenu->addAction(tr("Export Template\xe2\x80\xa6"), this, &MainWindow::onExportTemplate);
    exportAction_->setEnabled(false);
    fileMenu->addAction(tr("Import Template\xe2\x80\xa6"), this, &MainWindow::onImportTemplate);

    auto* editMenu = menuBar()->addMenu(tr("&Edit"));
    auto* settingsAction = editMenu->addAction(tr("Settings\xe2\x80\xa6"), this, &MainWindow::onSettingsClicked);
    settingsAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Comma));

    auto* helpMenu = menuBar()->addMenu(tr("&Help"));
    helpMenu->addAction(tr("About MonDoc"), this, &MainWindow::onAboutClicked);

    auto* tb = addToolBar(tr("Main"));
    tb->setMovable(false);
    auto* regBtn = new QPushButton(tr("Register Template"), tb);
    regBtn->setShortcut(QKeySequence("Ctrl+O"));
    regBtn->setStyleSheet(
        QStringLiteral("QPushButton { background-color: #2563EB; color: white; "
                       "padding: 6px 12px; }"));
    regBtn->setAccessibleName(tr("Register Template"));
    regBtn->setToolTip(tr("Register Template"));
    tb->addWidget(regBtn);

    connect(regBtn, &QPushButton::clicked, this, &MainWindow::onRegisterClicked);
    connect(searchBox_, &QLineEdit::textChanged, this, &MainWindow::onSearchChanged);
    connect(templateList_, &QListWidget::currentRowChanged,
            this, &MainWindow::onTemplateSelected);
    connect(schemaWidget_, &SchemaDockWidget::schemaSaved,
            this, &MainWindow::onSchemaSaved);
    connect(schemaWidget_, &SchemaDockWidget::schemaDiscarded,
            this, &MainWindow::onSchemaDiscarded);

    templateList_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(templateList_, &QListWidget::customContextMenuRequested,
            this, &MainWindow::showListContextMenu);

    auto* deleteAction = new QAction(tr("Delete Template"), templateList_);
    deleteAction->setShortcut(QKeySequence::Delete);
    deleteAction->setShortcutContext(Qt::WidgetShortcut);
    connect(deleteAction, &QAction::triggered, this, &MainWindow::onDeleteTemplate);
    templateList_->addAction(deleteAction);

    connect(fillSessionView_, &FillSessionView::backRequested,
            this, &MainWindow::onSessionBackRequested);
    connect(fillSessionView_, &FillSessionView::draftSaved,
            this, &MainWindow::onSessionDraftSaved);
    connect(fillSessionView_, &FillSessionView::sessionExported,
            this, &MainWindow::onSessionExported);
    connect(fillSessionView_, &FillSessionView::exportFailed,
            this, &MainWindow::onSessionExportFailed);
    connect(resumeBanner_, &ResumeBanner::resumeRequested,
            this, &MainWindow::onResumeRequested);
    connect(resumeBanner_, &ResumeBanner::discardRequested,
            this, &MainWindow::onDiscardRequested);

    refreshTemplateList();
    refreshResumeBanner();
}

void MainWindow::buildSidebar(QWidget* sidebar) {
    auto* layout = new QVBoxLayout(sidebar);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);

    searchBox_->setPlaceholderText(tr("Search templates..."));
    searchBox_->setAccessibleName(tr("Search templates"));
    layout->addWidget(searchBox_);
    layout->addWidget(resumeBanner_);

    templateList_->setSelectionMode(QAbstractItemView::SingleSelection);
    templateList_->setAccessibleName(tr("Template list"));
    layout->addWidget(templateList_, 1);
}

void MainWindow::buildEmptyState(QWidget* page) {
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->addStretch(1);

    auto* heading = new QLabel(tr("No templates yet"), page);
    QFont headingFont = heading->font();
    headingFont.setBold(true);
    headingFont.setPointSize(headingFont.pointSize() + 2);
    heading->setFont(headingFont);
    heading->setAlignment(Qt::AlignCenter);

    auto* body = new QLabel(
        tr("Drag a file here, or click Register Template to get started."), page);
    body->setAlignment(Qt::AlignCenter);
    body->setWordWrap(true);

    layout->addWidget(heading);
    layout->addWidget(body);
    layout->addStretch(2);
}

void MainWindow::buildDetailPage(QWidget* page) {
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(12);

    detailNameLabel_ = new QLabel(page);
    QFont nameFont = detailNameLabel_->font();
    nameFont.setBold(true);
    nameFont.setPointSize(nameFont.pointSize() + 2);
    detailNameLabel_->setFont(nameFont);
    layout->addWidget(detailNameLabel_);

    auto* metaRow = new QHBoxLayout();
    detailFormatLabel_ = new QLabel(page);
    detailFieldCountLabel_ = new QLabel(page);
    detailCreatedLabel_ = new QLabel(page);
    metaRow->addWidget(detailFormatLabel_);
    metaRow->addSpacing(16);
    metaRow->addWidget(detailFieldCountLabel_);
    metaRow->addSpacing(16);
    metaRow->addWidget(detailCreatedLabel_);
    metaRow->addStretch(1);
    layout->addLayout(metaRow);

    auto* sep = new QFrame(page);
    sep->setFrameShape(QFrame::HLine);
    sep->setFrameShadow(QFrame::Sunken);
    layout->addWidget(sep);

    auto* primaryRow = new QHBoxLayout();
    detailFillBtn_ = new QPushButton(tr("Fill Session\xe2\x80\xa6"), page);
    detailFillBtn_->setStyleSheet(
        QStringLiteral("QPushButton { background-color: #2563EB; color: white; "
                       "padding: 6px 12px; }"));
    detailFillBtn_->setShortcut(QKeySequence(QStringLiteral("Ctrl+F")));
    detailFillBtn_->setAccessibleName(tr("Fill Session"));
    detailFillBtn_->setToolTip(tr("Fill Session"));
    primaryRow->addWidget(detailFillBtn_);
    primaryRow->addStretch(1);
    layout->addLayout(primaryRow);

    connect(detailFillBtn_, &QPushButton::clicked,
            this, &MainWindow::onFillSessionClicked);

    auto* actionRow = new QHBoxLayout();
    detailRenameBtn_ = new QPushButton(tr("Rename Template"), page);
    detailDuplicateBtn_ = new QPushButton(tr("Duplicate Template"), page);
    detailDeleteBtn_ = new QPushButton(tr("Delete Template"), page);
    detailDeleteBtn_->setStyleSheet(QStringLiteral("color: #DC2626;"));
    auto* markRegionBtn = new QPushButton(tr("Mark Region\xe2\x80\xa6"), page);
    markRegionBtn->setAccessibleName(tr("Mark a fillable region in the template document"));
    markRegionBtn->setToolTip(tr("Open the source document and drag to mark a fillable region"));
    actionRow->addWidget(detailRenameBtn_);
    actionRow->addWidget(detailDuplicateBtn_);
    actionRow->addWidget(markRegionBtn);
    actionRow->addWidget(detailDeleteBtn_);
    actionRow->addStretch(1);
    layout->addLayout(actionRow);

    layout->addStretch(1);

    connect(detailRenameBtn_, &QPushButton::clicked,
            this, &MainWindow::onRenameTemplate);
    connect(detailDuplicateBtn_, &QPushButton::clicked,
            this, &MainWindow::onDuplicateTemplate);
    connect(detailDeleteBtn_, &QPushButton::clicked,
            this, &MainWindow::onDeleteTemplate);
    connect(markRegionBtn, &QPushButton::clicked,
            this, &MainWindow::onMarkRegion);
}

void MainWindow::refreshTemplateList() {
    auto previousId = selectedTemplateId();
    templateList_->blockSignals(true);
    templateList_->clear();

    auto result = service_.listTemplates();
    if (!result) {
        QMessageBox::critical(this, tr("MonDoc"),
            QString::fromStdString(result.error().message()));
        templateList_->blockSignals(false);
        centralStack_->setCurrentIndex(0);
        return;
    }

    cachedTemplates_ = std::move(*result);

    int restoreRow = -1;
    for (int i = 0; i < static_cast<int>(cachedTemplates_.size()); ++i) {
        const auto& tpl = cachedTemplates_[i];
        auto* item = new QListWidgetItem(QString::fromStdString(tpl.name_));
        item->setData(kTemplateIdRole, QString::fromStdString(tpl.id_.value()));
        templateList_->addItem(item);
        if (previousId && tpl.id_ == *previousId) {
            restoreRow = i;
        }
    }
    templateList_->blockSignals(false);

    if (restoreRow >= 0) {
        templateList_->setCurrentRow(restoreRow);
    } else if (templateList_->count() == 0) {
        selectedTemplate_.reset();
        centralStack_->setCurrentIndex(0);
    } else {
        templateList_->setCurrentRow(0);
    }
    onSearchChanged(searchBox_->text());
}

std::optional<mondoc::TemplateId> MainWindow::selectedTemplateId() const {
    auto* item = templateList_->currentItem();
    if (!item) return std::nullopt;
    const QString idStr = item->data(kTemplateIdRole).toString();
    if (idStr.isEmpty()) return std::nullopt;
    return mondoc::TemplateId{idStr.toStdString()};
}

void MainWindow::setAiFieldDetector(mondoc::adapters::ai::AiFieldDetector* detector) {
    schemaWidget_->setDetector(detector);
    schemaWidget_->setAiConfigured(detector != nullptr);
}

void MainWindow::onRegisterClicked() {
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Register Template"), QString{},
        tr("Documents (*.docx *.odt *.pdf *.txt *.md)"));
    if (path.isEmpty()) return;
    triggerRegistration(qStringToPath(path));
}

void MainWindow::triggerRegistration(const std::filesystem::path& path) {
    const QFileInfo info(pathToQString(path));
    statusBar()->showMessage(
        tr("Reading %1\xe2\x80\xa6").arg(info.fileName()));

    auto result = service_.extractDraft(path);
    if (!result) {
        statusBar()->showMessage(
            tr("Could not read %1. The file may be corrupted or unsupported. "
               "Try converting to .docx, .odt, .pdf, .txt, or .md first.")
                .arg(info.fileName()));
        return;
    }

    pendingTemplate_ = std::move(result->draft);
    pendingDocumentText_ = std::move(result->document_text);
    const QString name = QString::fromStdString(pendingTemplate_.name_).left(40);
    schemaWidget_->setWindowTitle(tr("Schema \xe2\x80\x94 %1").arg(name));
    schemaWidget_->setDocumentText(pendingDocumentText_);
    schemaWidget_->populate(pendingTemplate_.fields_);
    schemaWidget_->show();
    schemaWidget_->raise();

    statusBar()->showMessage(
        tr("%1 registered. %n field(s) found.", "",
           static_cast<int>(pendingTemplate_.fields_.size()))
            .arg(QString::fromStdString(pendingTemplate_.name_)));
}

void MainWindow::onSchemaSaved() {
    pendingTemplate_.fields_ = schemaWidget_->currentFields();
    auto result = service_.saveTemplate(pendingTemplate_);
    if (!result) {
        QMessageBox::critical(this, tr("MonDoc"),
            QString::fromStdString(result.error().message()));
        return;
    }
    schemaWidget_->hide();
    const QString savedName = QString::fromStdString(pendingTemplate_.name_);
    refreshTemplateList();
    statusBar()->showMessage(
        tr("\xe2\x80\x9c%1\xe2\x80\x9d saved to library.").arg(savedName));
}

void MainWindow::onSchemaDiscarded() {
    schemaWidget_->hide();
}

void MainWindow::onTemplateSelected(int row) {
    if (row < 0 || row >= static_cast<int>(cachedTemplates_.size())) {
        selectedTemplate_.reset();
        if (exportAction_) exportAction_->setEnabled(false);
        centralStack_->setCurrentIndex(0);
        return;
    }
    selectedTemplate_ = cachedTemplates_[row];
    if (exportAction_) exportAction_->setEnabled(true);
    const auto& t = *selectedTemplate_;
    detailNameLabel_->setText(QString::fromStdString(t.name_));
    detailFormatLabel_->setText(
        tr("Format:") + QStringLiteral(" ") +
        QString::fromStdString(t.source_format_));
    detailFieldCountLabel_->setText(
        tr("%n field(s) extracted", "", static_cast<int>(t.fields_.size())));
    detailCreatedLabel_->setText(tr("Added:"));
    centralStack_->setCurrentIndex(1);
}

void MainWindow::onSearchChanged(const QString& text) {
    for (int i = 0; i < templateList_->count(); ++i) {
        auto* item = templateList_->item(i);
        const bool match =
            text.isEmpty() || item->text().contains(text, Qt::CaseInsensitive);
        item->setHidden(!match);
    }
}

void MainWindow::onRenameTemplate() {
    auto id = selectedTemplateId();
    if (!id) return;
    const QString currentName = templateList_->currentItem()->text();
    bool ok = false;
    const QString newName = QInputDialog::getText(
        this, tr("Rename Template"),
        tr("New name for \xe2\x80\x9c%1\xe2\x80\x9d:").arg(currentName),
        QLineEdit::Normal, currentName, &ok);
    if (!ok || newName.trimmed().isEmpty()) return;

    auto result = service_.renameTemplate(*id, newName.trimmed().toStdString());
    if (!result) {
        QMessageBox::critical(this, tr("MonDoc"),
            QString::fromStdString(result.error().message()));
        return;
    }
    refreshTemplateList();
}

void MainWindow::onDuplicateTemplate() {
    auto id = selectedTemplateId();
    if (!id) return;
    const QString currentName = templateList_->currentItem()->text();
    bool ok = false;
    const QString newName = QInputDialog::getText(
        this, tr("Duplicate Template"),
        tr("Name for the copy:"),
        QLineEdit::Normal,
        tr("%1 Copy").arg(currentName), &ok);
    if (!ok || newName.trimmed().isEmpty()) return;

    auto result = service_.duplicateTemplate(*id, newName.trimmed().toStdString());
    if (!result) {
        QMessageBox::critical(this, tr("MonDoc"),
            QString::fromStdString(result.error().message()));
        return;
    }
    refreshTemplateList();
}

void MainWindow::onDeleteTemplate() {
    auto id = selectedTemplateId();
    if (!id) return;
    const QString currentName = templateList_->currentItem()->text();

    QMessageBox box(this);
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle(tr("Delete Template"));
    box.setText(
        tr("Delete \xe2\x80\x9c%1\xe2\x80\x9d? This cannot be undone.")
            .arg(currentName));
    auto* deleteBtn = box.addButton(tr("Delete Template"),
                                    QMessageBox::DestructiveRole);
    box.addButton(tr("Keep Template"), QMessageBox::RejectRole);
    box.exec();
    if (box.clickedButton() != deleteBtn) return;

    auto result = service_.deleteTemplate(*id);
    if (!result) {
        QMessageBox::critical(this, tr("MonDoc"),
            QString::fromStdString(result.error().message()));
        return;
    }
    refreshTemplateList();
    refreshResumeBanner();
    if (templateList_->count() == 0) {
        centralStack_->setCurrentIndex(0);
    }
}

void MainWindow::showListContextMenu(const QPoint& pos) {
    auto* item = templateList_->itemAt(pos);
    if (!item) return;
    templateList_->setCurrentItem(item);

    QMenu menu(this);
    menu.addAction(tr("Rename Template"), this, &MainWindow::onRenameTemplate);
    menu.addAction(tr("Duplicate Template"), this, &MainWindow::onDuplicateTemplate);
    menu.addSeparator();
    auto* del = menu.addAction(tr("Delete Template"),
                               this, &MainWindow::onDeleteTemplate);
    del->setIcon(QApplication::style()->standardIcon(QStyle::SP_TrashIcon));
    menu.exec(templateList_->viewport()->mapToGlobal(pos));
}

bool MainWindow::acceptableDrop(const QList<QUrl>& urls) const {
    for (const auto& url : urls) {
        if (hasAcceptedExtension(url)) return true;
    }
    return false;
}

void MainWindow::setDropHighlight(bool active) {
    if (active) {
        centralWidget()->setStyleSheet(
            QStringLiteral("QSplitter { border: 2px solid #2563EB; }"));
    } else {
        centralWidget()->setStyleSheet(QString{});
    }
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
    if (!event->mimeData()->hasUrls()) return;
    if (!acceptableDrop(event->mimeData()->urls())) {
        statusBar()->showMessage(
            tr("Unsupported file type. Drop a .docx, .odt, .pdf, .txt, or .md file."));
        return;
    }
    statusBar()->showMessage(tr("Drop to register as a template"));
    setDropHighlight(true);
    event->acceptProposedAction();
}

void MainWindow::dragMoveEvent(QDragMoveEvent* event) {
    if (event->mimeData()->hasUrls() &&
        acceptableDrop(event->mimeData()->urls())) {
        event->acceptProposedAction();
    }
}

void MainWindow::dragLeaveEvent(QDragLeaveEvent*) {
    setDropHighlight(false);
}

void MainWindow::dropEvent(QDropEvent* event) {
    setDropHighlight(false);
    if (!event->mimeData()->hasUrls()) return;
    for (const auto& url : event->mimeData()->urls()) {
        if (!hasAcceptedExtension(url)) continue;
        triggerRegistration(qStringToPath(url.toLocalFile()));
    }
    event->acceptProposedAction();
}

void MainWindow::onFillSessionClicked() {
    auto id = selectedTemplateId();
    if (!id) return;

    auto sessionId = fillService_.openSession(*id);
    if (!sessionId) {
        QMessageBox::critical(this, tr("MonDoc"),
            QString::fromStdString(sessionId.error().message()));
        return;
    }

    const QStringList paths = QFileDialog::getOpenFileNames(
        this, tr("Attach source documents (optional)"), QString{},
        tr("Source documents (*.docx *.txt *.md)"));
    std::vector<std::filesystem::path> sourcePaths;
    sourcePaths.reserve(static_cast<std::size_t>(paths.size()));
    for (const auto& p : paths) {
        sourcePaths.push_back(qStringToPath(p));
    }

    QString err;
    if (!fillSessionView_->openSession(*sessionId, sourcePaths, &err)) {
        QMessageBox::critical(this, tr("MonDoc"), err);
        (void)fillService_.discardSession(*sessionId);
        return;
    }
    centralStack_->setCurrentIndex(2);
}

void MainWindow::onSessionBackRequested() {
    fillSessionView_->clearSession();
    centralStack_->setCurrentIndex(templateList_->count() == 0 ? 0 : 1);
    refreshResumeBanner();
}

void MainWindow::onSessionDraftSaved() {
    statusBar()->showMessage(tr("Draft saved."), 2000);
}

void MainWindow::onSessionExported(QString fileName) {
    fillSessionView_->clearSession();
    centralStack_->setCurrentIndex(templateList_->count() == 0 ? 0 : 1);
    refreshResumeBanner();
    statusBar()->showMessage(
        tr("\xe2\x80\x9c%1\xe2\x80\x9d exported.").arg(fileName), 4000);
}

void MainWindow::onSessionExportFailed(QString message) {
    statusBar()->showMessage(tr("Export failed: %1").arg(message), 6000);
}

void MainWindow::onResumeRequested(mondoc::FillSessionId sessionId) {
    QString err;
    std::vector<std::filesystem::path> sourcePaths;
    if (!fillSessionView_->openSession(sessionId, sourcePaths, &err)) {
        QMessageBox::critical(this, tr("MonDoc"), err);
        return;
    }
    centralStack_->setCurrentIndex(2);
}

void MainWindow::onDiscardRequested(mondoc::FillSessionId sessionId) {
    auto r = fillService_.discardSession(sessionId);
    if (!r) {
        QMessageBox::warning(this, tr("MonDoc"),
            QString::fromStdString(r.error().message()));
    }
    refreshResumeBanner();
}

void MainWindow::refreshResumeBanner() {
    auto drafts = fillService_.listDrafts();
    if (!drafts) {
        resumeBanner_->setDrafts({});
        return;
    }
    std::vector<DraftSummary> rows;
    rows.reserve(drafts->size());
    for (const auto& s : *drafts) {
        DraftSummary d;
        d.sessionId = s.id_;
        auto tpl = templateRepo_.findById(s.template_id_);
        d.templateName = tpl
            ? QString::fromStdString(tpl->name_)
            : QString::fromStdString(s.template_id_.value());
        d.relativeTimestamp = relativeTimestamp(s.updated_at_unix_);
        rows.push_back(std::move(d));
    }
    resumeBanner_->setDrafts(rows);
}

QString MainWindow::relativeTimestamp(std::int64_t updatedAtUnix) const {
    using namespace std::chrono;
    const auto now = duration_cast<seconds>(
        system_clock::now().time_since_epoch()).count();
    const auto delta = now - updatedAtUnix;
    if (delta < 60)        return tr("just now");
    if (delta < 3600)      return tr("%n minute(s) ago", "",
                                       static_cast<int>(delta / 60));
    if (delta < 86400)     return tr("%n hour(s) ago", "",
                                       static_cast<int>(delta / 3600));
    if (delta < 7 * 86400) return tr("%n day(s) ago", "",
                                       static_cast<int>(delta / 86400));
    const std::time_t t = static_cast<std::time_t>(updatedAtUnix);
    char buf[32];
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tm);
    return QString::fromUtf8(buf);
}

void MainWindow::onSettingsClicked() {
    auto* dlg = new SettingsDialog(currentConfig_, this);
    connect(dlg, &SettingsDialog::settingsSaved, this,
            [this](mondoc::adapters::ai::LlmConfig cfg) {
                currentConfig_ = cfg;
                if (reconfigureLlmCallback_) reconfigureLlmCallback_(std::move(cfg));
                statusBar()->showMessage(tr("Settings saved."), 3000);
            });
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->exec();
}

void MainWindow::onExportTemplate() {
    if (!selectedTemplate_) return;
    const QString dest = QFileDialog::getSaveFileName(
        this, tr("Export Template"), QString{},
        tr("MonDoc Bundle (*.mondoc)"));
    if (dest.isEmpty()) return;
    auto result = service_.exportTemplate(selectedTemplate_->id_, qStringToPath(dest));
    if (result) {
        statusBar()->showMessage(tr("Template exported."), 3000);
    } else {
        statusBar()->showMessage(
            tr("Export failed: %1").arg(QString::fromStdString(result.error().message())), 5000);
    }
}

void MainWindow::onImportTemplate() {
    const QString src = QFileDialog::getOpenFileName(
        this, tr("Import Template"), QString{},
        tr("MonDoc Bundle (*.mondoc)"));
    if (src.isEmpty()) return;

    auto result = service_.importTemplate(qStringToPath(src));
    if (!result) {
        if (result.error().kind() == mondoc::Error::Kind::Conflict) {
            const QString name = QString::fromStdString(result.error().message());
            ImportConflictDialog dlg(name, this);
            if (dlg.exec() != QDialog::Accepted) return;
            const bool overwrite = dlg.choice() == ConflictChoice::Overwrite;
            const bool copy      = dlg.choice() == ConflictChoice::Copy;
            result = service_.importTemplate(qStringToPath(src), overwrite, copy);
        }
        if (!result) {
            statusBar()->showMessage(
                tr("Import failed: %1").arg(QString::fromStdString(result.error().message())), 5000);
            return;
        }
    }
    statusBar()->showMessage(
        tr("Template \"%1\" imported.").arg(QString::fromStdString(result->name_)), 3000);
    refreshTemplateList();
}

void MainWindow::onMarkRegion() {
    if (!selectedTemplate_) return;
    RegionMarkViewer dlg(selectedTemplate_->source_path_,
                         QString::fromStdString(selectedTemplate_->name_), this);
    if (dlg.exec() != QDialog::Accepted) return;
    pendingTemplate_ = *selectedTemplate_;
    pendingDocumentText_.clear();
    schemaWidget_->setDocumentText(pendingDocumentText_);
    schemaWidget_->populate(pendingTemplate_.fields_);
    schemaWidget_->addFieldExternal(dlg.field());
    schemaWidget_->show();
}

void MainWindow::onAboutClicked() {
    AboutDialog dlg(this);
    dlg.exec();
}

}  // namespace mondoc::ui
