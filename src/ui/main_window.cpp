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

#include <set>
#include <string>

#include "schema_dock_widget.hpp"

namespace mondoc::ui {

namespace {

constexpr int kTemplateIdRole = Qt::UserRole + 1;

const std::set<QString>& acceptedExtensions() {
    static const std::set<QString> exts{
        QStringLiteral("docx"),
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

MainWindow::MainWindow(mondoc::services::TemplateService& service, QWidget* parent)
    : QMainWindow(parent),
      service_(service),
      templateList_(new QListWidget(this)),
      centralStack_(new QStackedWidget(this)),
      searchBox_(new QLineEdit(this)),
      schemaWidget_(new SchemaDockWidget(this)),
      detailNameLabel_(nullptr),
      detailFormatLabel_(nullptr),
      detailFieldCountLabel_(nullptr),
      detailCreatedLabel_(nullptr),
      detailRenameBtn_(nullptr),
      detailDuplicateBtn_(nullptr),
      detailDeleteBtn_(nullptr) {
    setWindowTitle(tr("MonDoc"));
    setAcceptDrops(true);

    auto* sidebar = new QWidget(this);
    sidebar->setMinimumWidth(180);
    buildSidebar(sidebar);

    auto* emptyPage = new QWidget(this);
    buildEmptyState(emptyPage);
    auto* detailPage = new QWidget(this);
    buildDetailPage(detailPage);
    centralStack_->addWidget(emptyPage);
    centralStack_->addWidget(detailPage);
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

    refreshTemplateList();
}

void MainWindow::buildSidebar(QWidget* sidebar) {
    auto* layout = new QVBoxLayout(sidebar);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);

    searchBox_->setPlaceholderText(tr("Search templates..."));
    searchBox_->setAccessibleName(tr("Search templates"));
    layout->addWidget(searchBox_);

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

    auto* actionRow = new QHBoxLayout();
    detailRenameBtn_ = new QPushButton(tr("Rename Template"), page);
    detailDuplicateBtn_ = new QPushButton(tr("Duplicate Template"), page);
    detailDeleteBtn_ = new QPushButton(tr("Delete Template"), page);
    detailDeleteBtn_->setStyleSheet(QStringLiteral("color: #DC2626;"));
    actionRow->addWidget(detailRenameBtn_);
    actionRow->addWidget(detailDuplicateBtn_);
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

void MainWindow::onRegisterClicked() {
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Register Template"), QString{},
        tr("Documents (*.docx *.txt *.md)"));
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
               "Try converting to .docx, .txt, or .md first.")
                .arg(info.fileName()));
        return;
    }

    pendingTemplate_ = std::move(*result);
    const QString name = QString::fromStdString(pendingTemplate_.name_).left(40);
    schemaWidget_->setWindowTitle(tr("Schema \xe2\x80\x94 %1").arg(name));
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
        centralStack_->setCurrentIndex(0);
        return;
    }
    selectedTemplate_ = cachedTemplates_[row];
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
            tr("Unsupported file type. Drop a .docx, .txt, or .md file."));
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

}  // namespace mondoc::ui
