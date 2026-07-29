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
#include <QSettings>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyle>
#include <QThread>
#include <QToolBar>
#include <QUrl>
#include <QVBoxLayout>
#include <QVariant>
#include <QWidget>
#include <QtGlobal>

#include <algorithm>
#include <chrono>
#include <ctime>
#include <string>

#include "about_dialog.hpp"
#include "document_canvas.hpp"
#include "fill_session_view.hpp"
#include "import_conflict_dialog.hpp"
#include "mondoc/util.hpp"
#include "pdf_document_reader.hpp"
#include "plain_text_extractor.hpp"
#include "preview_anchor.hpp"
#include "preview_provider.hpp"
#include "preview_worker.hpp"
#include "region_mark_viewer.hpp"
#include "resume_banner.hpp"
#include "schema_dock_widget.hpp"
#include "settings_dialog.hpp"
#include "supported_formats.hpp"
#include "ui_style.hpp"

namespace mondoc::ui {

namespace {

constexpr int kTemplateIdRole = Qt::UserRole + 1;

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

bool anyFieldMissingLocation(const mondoc::domain::Template& t) {
    return std::any_of(t.fields_.begin(), t.fields_.end(),
                       [](const auto& f) { return !f.location_.has_value(); });
}

bool anyFieldLocated(const mondoc::domain::Template& t) {
    return std::any_of(t.fields_.begin(), t.fields_.end(),
                       [](const auto& f) { return f.location_.has_value(); });
}

}  // namespace

MainWindow::MainWindow(mondoc::services::TemplateService& templateService,
                       mondoc::services::FillSessionService& fillService,
                       mondoc::domain::ITemplateRepository& templateRepo,
                       const mondoc::adapters::ai::LlmConfig& currentConfig,
                       std::function<void(mondoc::adapters::ai::LlmConfig)> reconfigureLlmCallback,
                       std::filesystem::path dataDir,
                       QWidget* parent)
    : QMainWindow(parent),
      service_(templateService),
      fill_service_(fillService),
      template_repo_(templateRepo),
      current_config_(currentConfig),
      reconfigure_llm_callback_(std::move(reconfigureLlmCallback)),
      data_dir_(std::move(dataDir)),
      template_list_(new QListWidget(this)),
      central_stack_(new QStackedWidget(this)),
      search_box_(new QLineEdit(this)),
      schema_widget_(new SchemaDockWidget(this)),
      fill_session_view_(nullptr),
      resume_banner_(nullptr),
      document_canvas_(nullptr),
      detail_name_label_(nullptr),
      detail_format_label_(nullptr),
      detail_field_count_label_(nullptr),
      detail_created_label_(nullptr),
      detail_fill_btn_(nullptr),
      detail_rename_btn_(nullptr),
      detail_duplicate_btn_(nullptr),
      detail_delete_btn_(nullptr),
      detect_positions_btn_(nullptr) {
    setWindowTitle(tr("MonDoc"));
    setAcceptDrops(true);

    fill_session_view_ = new FillSessionView(fill_service_, template_repo_, this);
    resume_banner_ = new ResumeBanner(this);

    auto* sidebar = new QWidget(this);
    sidebar->setMinimumWidth(180);
    buildSidebar(sidebar);

    auto* emptyPage = new QWidget(this);
    buildEmptyState(emptyPage);
    auto* detailPage = new QWidget(this);
    buildDetailPage(detailPage);
    central_stack_->addWidget(emptyPage);
    central_stack_->addWidget(detailPage);
    central_stack_->addWidget(fill_session_view_);
    central_stack_->setCurrentIndex(0);

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->addWidget(sidebar);
    splitter->addWidget(central_stack_);
    splitter->setChildrenCollapsible(false);
    splitter->setSizes({240, 800});
    setCentralWidget(splitter);

    schema_widget_->setWindowTitle(tr("Schema"));
    addDockWidget(Qt::RightDockWidgetArea, schema_widget_);
    schema_widget_->hide();

    statusBar();

    // Menu bar
    auto* fileMenu = menuBar()->addMenu(tr("&File"));
    export_action_ = fileMenu->addAction(tr("Export Template\xe2\x80\xa6"), this, &MainWindow::onExportTemplate);
    export_action_->setEnabled(false);
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
    regBtn->setStyleSheet(accentButtonStyle());
    regBtn->setAccessibleName(tr("Register Template"));
    regBtn->setToolTip(tr("Register Template"));
    tb->addWidget(regBtn);

    connect(regBtn, &QPushButton::clicked, this, &MainWindow::onRegisterClicked);
    connect(search_box_, &QLineEdit::textChanged, this, &MainWindow::onSearchChanged);
    connect(template_list_, &QListWidget::currentRowChanged,
            this, &MainWindow::onTemplateSelected);
    connect(schema_widget_, &SchemaDockWidget::schemaSaved,
            this, &MainWindow::onSchemaSaved);
    connect(schema_widget_, &SchemaDockWidget::schemaDiscarded,
            this, &MainWindow::onSchemaDiscarded);
    connect(schema_widget_, &SchemaDockWidget::rowSelected,
            this, &MainWindow::onSchemaRowSelected);

    template_list_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(template_list_, &QListWidget::customContextMenuRequested,
            this, &MainWindow::showListContextMenu);

    auto* deleteAction = new QAction(tr("Delete Template"), template_list_);
    deleteAction->setShortcut(QKeySequence::Delete);
    deleteAction->setShortcutContext(Qt::WidgetShortcut);
    connect(deleteAction, &QAction::triggered, this, &MainWindow::onDeleteTemplate);
    template_list_->addAction(deleteAction);

    connect(fill_session_view_, &FillSessionView::backRequested,
            this, &MainWindow::onSessionBackRequested);
    connect(fill_session_view_, &FillSessionView::sessionExported,
            this, &MainWindow::onSessionExported);
    connect(fill_session_view_, &FillSessionView::exportFailed,
            this, &MainWindow::onSessionExportFailed);
    connect(fill_session_view_, &FillSessionView::statusMessageRequested,
            statusBar(), &QStatusBar::showMessage);
    connect(resume_banner_, &ResumeBanner::resumeRequested,
            this, &MainWindow::onResumeRequested);
    connect(resume_banner_, &ResumeBanner::discardRequested,
            this, &MainWindow::onDiscardRequested);

    refreshTemplateList();
    refreshResumeBanner();
}

MainWindow::~MainWindow() {
    // C-3: no event-loop turn separates this from main.cpp destroying the
    // CompositionRoot right after the window, so a still-running preview
    // conversion must be waited out rather than abandoned.
    shutdownPreviewThread(/*mustJoin=*/true);
}

void MainWindow::buildSidebar(QWidget* sidebar) {
    auto* layout = new QVBoxLayout(sidebar);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);

    search_box_->setPlaceholderText(tr("Search templates..."));
    search_box_->setAccessibleName(tr("Search templates"));
    layout->addWidget(search_box_);
    layout->addWidget(resume_banner_);

    template_list_->setSelectionMode(QAbstractItemView::SingleSelection);
    template_list_->setAccessibleName(tr("Template list"));
    layout->addWidget(template_list_, 1);
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

    detail_name_label_ = new QLabel(page);
    QFont nameFont = detail_name_label_->font();
    nameFont.setBold(true);
    nameFont.setPointSize(nameFont.pointSize() + 2);
    detail_name_label_->setFont(nameFont);
    layout->addWidget(detail_name_label_);

    auto* metaRow = new QHBoxLayout();
    detail_format_label_ = new QLabel(page);
    detail_field_count_label_ = new QLabel(page);
    detail_created_label_ = new QLabel(page);
    metaRow->addWidget(detail_format_label_);
    metaRow->addSpacing(16);
    metaRow->addWidget(detail_field_count_label_);
    metaRow->addSpacing(16);
    metaRow->addWidget(detail_created_label_);
    metaRow->addStretch(1);
    layout->addLayout(metaRow);

    auto* sep = new QFrame(page);
    sep->setFrameShape(QFrame::HLine);
    sep->setFrameShadow(QFrame::Sunken);
    layout->addWidget(sep);

    document_canvas_ = new DocumentCanvas(page);
    document_canvas_->hide();
    layout->addWidget(document_canvas_, 1);

    connect(document_canvas_, &DocumentCanvas::frameDrawn,
            this, &MainWindow::onCanvasFrameDrawn);
    connect(document_canvas_, &DocumentCanvas::frameSelected,
            this, &MainWindow::onCanvasFrameSelected);
    connect(document_canvas_, &DocumentCanvas::frameChanged,
            this, &MainWindow::onCanvasFrameChanged);

    auto* primaryRow = new QHBoxLayout();
    detail_fill_btn_ = new QPushButton(tr("Fill Session\xe2\x80\xa6"), page);
    detail_fill_btn_->setStyleSheet(accentButtonStyle());
    detail_fill_btn_->setShortcut(QKeySequence(QStringLiteral("Ctrl+F")));
    detail_fill_btn_->setAccessibleName(tr("Fill Session"));
    detail_fill_btn_->setToolTip(tr("Fill Session"));
    primaryRow->addWidget(detail_fill_btn_);
    primaryRow->addStretch(1);
    layout->addLayout(primaryRow);

    connect(detail_fill_btn_, &QPushButton::clicked,
            this, &MainWindow::onFillSessionClicked);

    auto* actionRow = new QHBoxLayout();
    detail_rename_btn_ = new QPushButton(tr("Rename Template"), page);
    detail_duplicate_btn_ = new QPushButton(tr("Duplicate Template"), page);
    detail_delete_btn_ = new QPushButton(tr("Delete Template"), page);
    detail_delete_btn_->setStyleSheet(QStringLiteral("color: #DC2626;"));
    auto* markRegionBtn = new QPushButton(tr("Mark Region\xe2\x80\xa6"), page);
    markRegionBtn->setAccessibleName(tr("Mark a fillable region in the template document"));
    markRegionBtn->setToolTip(tr("Open the source document and drag to mark a fillable region"));
    detect_positions_btn_ = new QPushButton(tr("Detect field positions"), page);
    detect_positions_btn_->setAccessibleName(tr("Detect field positions"));
    detect_positions_btn_->setToolTip(
        tr("Re-scan the PDF's form fields and match them to the schema by name"));
    detect_positions_btn_->hide();
    actionRow->addWidget(detail_rename_btn_);
    actionRow->addWidget(detail_duplicate_btn_);
    actionRow->addWidget(markRegionBtn);
    actionRow->addWidget(detect_positions_btn_);
    actionRow->addWidget(detail_delete_btn_);
    actionRow->addStretch(1);
    layout->addLayout(actionRow);

    connect(detail_rename_btn_, &QPushButton::clicked,
            this, &MainWindow::onRenameTemplate);
    connect(detail_duplicate_btn_, &QPushButton::clicked,
            this, &MainWindow::onDuplicateTemplate);
    connect(detail_delete_btn_, &QPushButton::clicked,
            this, &MainWindow::onDeleteTemplate);
    connect(markRegionBtn, &QPushButton::clicked,
            this, &MainWindow::onMarkRegion);
    connect(detect_positions_btn_, &QPushButton::clicked,
            this, &MainWindow::onDetectFieldPositions);
}

void MainWindow::refreshTemplateList() {
    auto previousId = selectedTemplateId();
    template_list_->blockSignals(true);
    template_list_->clear();

    auto result = service_.listTemplates();
    if (!result) {
        QMessageBox::critical(this, tr("MonDoc"),
            QString::fromStdString(result.error().message()));
        template_list_->blockSignals(false);
        central_stack_->setCurrentIndex(0);
        return;
    }

    cached_templates_ = std::move(*result);

    int restoreRow = -1;
    for (int i = 0; i < static_cast<int>(cached_templates_.size()); ++i) {
        const auto& tpl = cached_templates_[i];
        auto* item = new QListWidgetItem(QString::fromStdString(tpl.name_));
        item->setData(kTemplateIdRole, QString::fromStdString(tpl.id_.value()));
        template_list_->addItem(item);
        if (previousId && tpl.id_ == *previousId) {
            restoreRow = i;
        }
    }
    template_list_->blockSignals(false);

    if (restoreRow >= 0) {
        template_list_->setCurrentRow(restoreRow);
    } else if (template_list_->count() == 0) {
        selected_template_.reset();
        central_stack_->setCurrentIndex(0);
    } else {
        template_list_->setCurrentRow(0);
    }
    onSearchChanged(search_box_->text());
}

std::optional<mondoc::TemplateId> MainWindow::selectedTemplateId() const {
    auto* item = template_list_->currentItem();
    if (!item) return std::nullopt;
    const QString idStr = item->data(kTemplateIdRole).toString();
    if (idStr.isEmpty()) return std::nullopt;
    return mondoc::TemplateId{idStr.toStdString()};
}

void MainWindow::setAiFieldDetector(mondoc::adapters::ai::AiFieldDetector* detector) {
    schema_widget_->setDetector(detector);
    schema_widget_->setAiConfigured(detector != nullptr);
}

void MainWindow::onRegisterClicked() {
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Register Template"), QString{},
        registrationDialogFilter());
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

    pending_template_ = std::move(result->draft);
    pending_document_text_ = std::move(result->document_text);
    const QString name = QString::fromStdString(pending_template_.name_).left(40);
    schema_widget_->setWindowTitle(tr("Schema \xe2\x80\x94 %1").arg(name));
    schema_widget_->setDocumentText(pending_document_text_);
    schema_widget_->populate(pending_template_.fields_);
    schema_widget_->show();
    schema_widget_->raise();

    statusBar()->showMessage(
        tr("%1 registered. %n field(s) found.", "",
           static_cast<int>(pending_template_.fields_.size()))
            .arg(QString::fromStdString(pending_template_.name_)));

    for (const auto& warning : pending_template_.warnings_) {
        QMessageBox::warning(this, tr("MonDoc"), QString::fromStdString(warning));
    }
}

void MainWindow::onSchemaSaved() {
    pending_template_.fields_ = schema_widget_->currentFields();
    auto result = service_.saveTemplate(pending_template_);
    if (!result) {
        QMessageBox::critical(this, tr("MonDoc"),
            QString::fromStdString(result.error().message()));
        return;
    }
    schema_widget_->hide();
    const QString savedName = QString::fromStdString(pending_template_.name_);
    refreshTemplateList();
    statusBar()->showMessage(
        tr("\xe2\x80\x9c%1\xe2\x80\x9d saved to library.").arg(savedName));
}

void MainWindow::onSchemaDiscarded() {
    schema_widget_->hide();
}

void MainWindow::onTemplateSelected(int row) {
    if (row < 0 || row >= static_cast<int>(cached_templates_.size())) {
        selected_template_.reset();
        if (export_action_) export_action_->setEnabled(false);
        central_stack_->setCurrentIndex(0);
        return;
    }
    selected_template_ = cached_templates_[row];
    if (export_action_) export_action_->setEnabled(true);
    const auto& t = *selected_template_;
    detail_name_label_->setText(QString::fromStdString(t.name_));
    detail_format_label_->setText(
        tr("Format:") + QStringLiteral(" ") +
        QString::fromStdString(t.source_format_));
    detail_field_count_label_->setText(
        tr("%n field(s) extracted", "", static_cast<int>(t.fields_.size())));
    detail_created_label_->setText(tr("Added:"));
    central_stack_->setCurrentIndex(1);

    detect_positions_btn_->setVisible(
        mondoc::hasExtension(t.source_path_, ".pdf") && anyFieldMissingLocation(t));

    preview_loaded_ = false;
    document_canvas_->hide();
    startPreview(t.id_, t.source_path_);
}

void MainWindow::onSearchChanged(const QString& text) {
    for (int i = 0; i < template_list_->count(); ++i) {
        auto* item = template_list_->item(i);
        const bool match =
            text.isEmpty() || item->text().contains(text, Qt::CaseInsensitive);
        item->setHidden(!match);
    }
}

void MainWindow::onRenameTemplate() {
    auto id = selectedTemplateId();
    if (!id) return;
    const QString currentName = template_list_->currentItem()->text();
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
    const QString currentName = template_list_->currentItem()->text();
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
    const QString currentName = template_list_->currentItem()->text();

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
    if (template_list_->count() == 0) {
        central_stack_->setCurrentIndex(0);
    }
}

void MainWindow::showListContextMenu(const QPoint& pos) {
    auto* item = template_list_->itemAt(pos);
    if (!item) return;
    template_list_->setCurrentItem(item);

    QMenu menu(this);
    menu.addAction(tr("Rename Template"), this, &MainWindow::onRenameTemplate);
    menu.addAction(tr("Duplicate Template"), this, &MainWindow::onDuplicateTemplate);
    menu.addSeparator();
    auto* del = menu.addAction(tr("Delete Template"),
                               this, &MainWindow::onDeleteTemplate);
    del->setIcon(QApplication::style()->standardIcon(QStyle::SP_TrashIcon));
    menu.exec(template_list_->viewport()->mapToGlobal(pos));
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
            QStringLiteral("QSplitter { border: 2px solid %1; }").arg(accentColor()));
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
    int acceptedCount = 0;
    for (const auto& url : event->mimeData()->urls()) {
        if (!hasAcceptedExtension(url)) continue;
        if (acceptedCount == 0) {
            triggerRegistration(qStringToPath(url.toLocalFile()));
        }
        ++acceptedCount;
    }
    if (acceptedCount > 1) {
        statusBar()->showMessage(
            tr("Registered first file; drop others individually."));
    }
    event->acceptProposedAction();
}

void MainWindow::onFillSessionClicked() {
    auto id = selectedTemplateId();
    if (!id) return;

    auto sessionId = fill_service_.openSession(*id);
    if (!sessionId) {
        QMessageBox::critical(this, tr("MonDoc"),
            QString::fromStdString(sessionId.error().message()));
        return;
    }

    const QStringList paths = QFileDialog::getOpenFileNames(
        this, tr("Attach source documents (optional)"), QString{},
        attachSourcesDialogFilter());
    std::vector<std::filesystem::path> sourcePaths;
    sourcePaths.reserve(static_cast<std::size_t>(paths.size()));
    for (const auto& p : paths) {
        sourcePaths.push_back(qStringToPath(p));
    }

    QString err;
    if (!fill_session_view_->openSession(*sessionId, sourcePaths, &err)) {
        QMessageBox::critical(this, tr("MonDoc"), err);
        if (auto discardResult = fill_service_.discardSession(*sessionId); !discardResult) {
            qWarning("MainWindow::onFillSessionClicked: failed to discard session "
                     "after failed open: %s", discardResult.error().message().c_str());
        }
        return;
    }
    central_stack_->setCurrentIndex(2);
}

void MainWindow::onSessionBackRequested() {
    fill_session_view_->clearSession();
    central_stack_->setCurrentIndex(template_list_->count() == 0 ? 0 : 1);
    refreshResumeBanner();
}

void MainWindow::onSessionExported(QString fileName) {
    fill_session_view_->clearSession();
    central_stack_->setCurrentIndex(template_list_->count() == 0 ? 0 : 1);
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
    if (!fill_session_view_->openSession(sessionId, sourcePaths, &err)) {
        QMessageBox::critical(this, tr("MonDoc"), err);
        return;
    }
    central_stack_->setCurrentIndex(2);
}

void MainWindow::onDiscardRequested(mondoc::FillSessionId sessionId) {
    auto r = fill_service_.discardSession(sessionId);
    if (!r) {
        QMessageBox::warning(this, tr("MonDoc"),
            QString::fromStdString(r.error().message()));
    }
    refreshResumeBanner();
}

void MainWindow::refreshResumeBanner() {
    auto drafts = fill_service_.listDrafts();
    if (!drafts) {
        resume_banner_->setDrafts({});
        return;
    }
    std::vector<DraftSummary> rows;
    rows.reserve(drafts->size());
    for (const auto& s : *drafts) {
        DraftSummary d;
        d.sessionId = s.id_;
        auto tpl = template_repo_.findById(s.template_id_);
        d.templateName = tpl
            ? QString::fromStdString(tpl->name_)
            : QString::fromStdString(s.template_id_.value());
        d.relativeTimestamp = relativeTimestamp(s.updated_at_unix_);
        rows.push_back(std::move(d));
    }
    resume_banner_->setDrafts(rows);
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
    SettingsDialog dlg(current_config_, this);
    connect(&dlg, &SettingsDialog::settingsSaved, this,
            [this](mondoc::adapters::ai::LlmConfig cfg) {
                current_config_ = cfg;
                if (reconfigure_llm_callback_) reconfigure_llm_callback_(std::move(cfg));
                statusBar()->showMessage(tr("Settings saved."), 3000);
            });
    dlg.exec();
}

void MainWindow::onExportTemplate() {
    if (!selected_template_) return;
    const QString dest = QFileDialog::getSaveFileName(
        this, tr("Export Template"), QString{},
        tr("MonDoc Bundle (*.mondoc)"));
    if (dest.isEmpty()) return;
    auto result = service_.exportTemplate(selected_template_->id_, qStringToPath(dest));
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
    if (!selected_template_) return;

    if (preview_loaded_) {
        document_canvas_->setFocus();
        statusBar()->showMessage(tr("Drag on the document to mark a field."));
        return;
    }

    // No preview available (LibreOffice missing/failed, or the source is
    // otherwise unrenderable) — fall back to the modal region marker.
    RegionMarkViewer dlg(selected_template_->source_path_,
                         QString::fromStdString(selected_template_->name_), this);
    if (dlg.exec() != QDialog::Accepted) return;
    pending_template_ = *selected_template_;
    pending_document_text_.clear();
    schema_widget_->setDocumentText(pending_document_text_);
    schema_widget_->populate(pending_template_.fields_);
    schema_widget_->addFieldExternal(dlg.field());
    schema_widget_->show();
}

void MainWindow::onDetectFieldPositions() {
    if (!selected_template_) return;

    mondoc::adapters::formats::PdfDocumentReader reader;
    auto result = reader.read(selected_template_->source_path_);
    if (!result) {
        statusBar()->showMessage(
            tr("Could not detect field positions: %1")
                .arg(QString::fromStdString(result.error().message())), 5000);
        return;
    }

    for (auto& field : selected_template_->fields_) {
        for (const auto& scanned : result->fields_) {
            if (scanned.name_ == field.name_ && scanned.location_) {
                field.location_ = scanned.location_;
                break;
            }
        }
    }

    auto saveResult = service_.saveTemplate(*selected_template_);
    if (!saveResult) {
        statusBar()->showMessage(
            tr("Could not save detected positions: %1")
                .arg(QString::fromStdString(saveResult.error().message())), 5000);
        return;
    }

    statusBar()->showMessage(tr("Field positions updated."), 3000);
    refreshTemplateList();
}

void MainWindow::startPreview(const mondoc::TemplateId& templateId,
                              const std::filesystem::path& source) {
    shutdownPreviewThread(/*mustJoin=*/false);
    ++preview_generation_;
    const int generation = preview_generation_;

    const QSettings appSettings(QStringLiteral("MonDoc"), QStringLiteral("MonDoc"));
    const QString overrideQs =
        appSettings.value(QStringLiteral("libreoffice/path")).toString();
    const std::filesystem::path sofficeOverride =
        overrideQs.isEmpty() ? std::filesystem::path{} : qStringToPath(overrideQs);
    const std::filesystem::path sofficePath =
        mondoc::adapters::formats::findLibreOffice(sofficeOverride);
    const std::filesystem::path cacheDir = data_dir_ / "previews";

    preview_thread_ = new QThread(this);
    preview_worker_ = new PreviewWorker(source, templateId.value(), cacheDir, sofficePath);
    preview_worker_->moveToThread(preview_thread_);

    connect(preview_thread_, &QThread::started, preview_worker_, &PreviewWorker::run);
    connect(preview_worker_, &PreviewWorker::finished, this,
            [this, generation](QString path, bool regenerated) {
                handlePreviewFinished(generation, path, regenerated);
            }, Qt::QueuedConnection);
    connect(preview_worker_, &PreviewWorker::failed, this,
            [this, generation](QString message) {
                handlePreviewFailed(generation, message);
            }, Qt::QueuedConnection);
    connect(preview_worker_, &PreviewWorker::finished, preview_thread_, &QThread::quit);
    connect(preview_worker_, &PreviewWorker::failed,   preview_thread_, &QThread::quit);
    connect(preview_thread_, &QThread::finished, preview_worker_, &QObject::deleteLater);
    connect(preview_thread_, &QThread::finished, preview_thread_, &QObject::deleteLater);
    connect(preview_thread_, &QThread::finished, this, [this]() {
        preview_thread_ = nullptr;
        preview_worker_ = nullptr;
    });

    preview_thread_->start();
}

void MainWindow::shutdownPreviewThread(bool mustJoin) {
    QThread* thread = preview_thread_;
    if (!thread) return;

    // Sever delivery to `this` before anything else: kills the
    // finished->lambda that nulls preview_thread_/preview_worker_ and stops
    // the worker's own finished/failed signals from reaching this window
    // once shutdown has begun. Connections from the worker/thread to each
    // other are left intact so an abandoned thread still exits and its
    // finished->deleteLater cleanup still runs.
    if (preview_worker_) preview_worker_->disconnect(this);
    thread->disconnect(this);
    preview_thread_ = nullptr;
    preview_worker_ = nullptr;

    if (!thread->isRunning()) return;

    thread->quit();
    if (thread->wait(5000)) return;

    if (mustJoin) {
        thread->wait();
        return;
    }

    // PreviewWorker has no cancellation hook (a LibreOffice conversion can't
    // be interrupted mid-flight): detach and let the existing
    // finished/failed -> thread->quit and thread->finished -> deleteLater
    // connections reap both objects once the conversion actually completes.
    thread->setParent(nullptr);
}

void MainWindow::handlePreviewFinished(int generation, const QString& previewPdfPath,
                                       bool regenerated) {
    if (generation != preview_generation_) return;
    if (!selected_template_) return;

    current_preview_path_ = qStringToPath(previewPdfPath);
    const bool hadLocatedFields = anyFieldLocated(*selected_template_);

    if (!document_canvas_->loadDocument(current_preview_path_)) {
        preview_loaded_ = false;
        document_canvas_->hide();
        statusBar()->showMessage(
            tr("Preview unavailable: %1").arg(tr("the document could not be rendered")));
        return;
    }

    preview_loaded_ = true;
    pending_template_ = *selected_template_;
    pending_document_text_.clear();
    schema_widget_->setDocumentText(pending_document_text_);
    schema_widget_->populate(pending_template_.fields_);

    document_canvas_->show();
    document_canvas_->setFrames(pending_template_.fields_);
    document_canvas_->setStaleWarning(regenerated && hadLocatedFields);
}

void MainWindow::handlePreviewFailed(int generation, const QString& message) {
    if (generation != preview_generation_) return;
    preview_loaded_ = false;
    document_canvas_->hide();
    statusBar()->showMessage(tr("Preview unavailable: %1").arg(message));
}

std::optional<mondoc::domain::TextLocation> MainWindow::computeTextAnchor(
        const mondoc::domain::PdfLocation& loc) const {
    if (!selected_template_ || current_preview_path_.empty()) return std::nullopt;
    auto text = mondoc::adapters::formats::extractPlainText(selected_template_->source_path_);
    return mondoc::adapters::formats::anchorForPreviewRect(
        current_preview_path_, loc, text.value_or(std::string{}));
}

void MainWindow::onCanvasFrameDrawn(mondoc::domain::PdfLocation loc) {
    if (!selected_template_) return;

    mondoc::domain::Field field;
    field.id_ = mondoc::FieldId{mondoc::generateUuid()};
    field.name_ = "field_" + std::to_string(pending_template_.fields_.size() + 1);
    field.type_ = mondoc::domain::FieldType::Text;
    field.origin_ = mondoc::domain::FieldOrigin::Unknown;

    QString statusMsg = tr("Field marked.");
    std::optional<mondoc::domain::TextLocation> anchor;
    if (!mondoc::hasExtension(selected_template_->source_path_, ".pdf")) {
        anchor = computeTextAnchor(loc);
        if (!anchor) statusMsg += tr(" (no fill anchor)");
    }
    field.location_ = mondoc::domain::FieldLocation{loc, anchor};

    pending_template_.fields_.push_back(field);
    schema_widget_->addFieldExternal(field);
    document_canvas_->setFrames(pending_template_.fields_);

    schema_widget_->show();
    schema_widget_->raise();
    statusBar()->showMessage(statusMsg);
}

void MainWindow::onCanvasFrameSelected(mondoc::FieldId id) {
    for (std::size_t row = 0; row < pending_template_.fields_.size(); ++row) {
        if (pending_template_.fields_[row].id_ == id) {
            schema_widget_->selectRow(static_cast<int>(row));
            return;
        }
    }
}

void MainWindow::onCanvasFrameChanged(mondoc::FieldId id, mondoc::domain::PdfLocation loc) {
    if (!selected_template_) return;
    for (auto& field : pending_template_.fields_) {
        if (field.id_ != id) continue;
        std::optional<mondoc::domain::TextLocation> anchor;
        if (!mondoc::hasExtension(selected_template_->source_path_, ".pdf")) {
            anchor = computeTextAnchor(loc);
        }
        field.location_ = mondoc::domain::FieldLocation{loc, anchor};
        schema_widget_->populate(pending_template_.fields_);
        return;
    }
}

void MainWindow::onSchemaRowSelected(int row) {
    if (row < 0 || row >= static_cast<int>(pending_template_.fields_.size())) return;
    document_canvas_->setSelectedField(
        pending_template_.fields_[static_cast<std::size_t>(row)].id_);
}

void MainWindow::onAboutClicked() {
    AboutDialog dlg(this);
    dlg.exec();
}

}  // namespace mondoc::ui
