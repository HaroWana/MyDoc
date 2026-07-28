#include "region_mark_viewer.hpp"

#include "mondoc/util.hpp"
#include "plain_text_extractor.hpp"

#include <QPainter>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QScrollArea>
#include <QPdfDocument>
#include <QTextBrowser>
#include <QTextCursor>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QMessageBox>
#include <QImage>
#include <QWidget>
#include <QColor>
#include <QPen>
#include <fstream>
#include <sstream>

#include "ui_style.hpp"

namespace mondoc::ui {

class PdfPageWidget : public QWidget {
    Q_OBJECT
public:
    explicit PdfPageWidget(QWidget* parent = nullptr)
        : QWidget(parent), dragging_(false), regionLocked_(false), pageIndex_(0)
    {
        setCursor(Qt::CrossCursor);
        setMouseTracking(false);
    }

    void setImage(const QImage& img, int pageIndex) {
        image_     = img;
        pageIndex_ = pageIndex;
        dragRect_  = QRect{};
        dragging_  = false;
        regionLocked_ = false;
        setFixedSize(image_.size());
        update();
    }

    QRect dragRect() const { return dragRect_; }
    QSize imageSize() const { return image_.size(); }
    int pageIndex() const { return pageIndex_; }
    bool hasRegion() const { return regionLocked_; }

signals:
    void regionDrawn(const QRect& rect);

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        if (!image_.isNull())
            p.drawImage(0, 0, image_);
        if (dragging_ || regionLocked_) {
            QColor fillColor(0x25, 0x63, 0xEB, 64);
            p.fillRect(dragRect_.normalized(), fillColor);
            p.setPen(QPen(QColor(0x25, 0x63, 0xEB), 2));
            p.drawRect(dragRect_.normalized());
        }
    }

    void mousePressEvent(QMouseEvent* e) override {
        if (e->button() != Qt::LeftButton) return;
        dragStart_ = e->pos();
        dragRect_  = QRect{dragStart_, dragStart_};
        dragging_  = true;
        regionLocked_ = false;
        update();
    }

    void mouseMoveEvent(QMouseEvent* e) override {
        if (!dragging_) return;
        dragRect_ = QRect{dragStart_, e->pos()}.normalized();
        update();
    }

    void mouseReleaseEvent(QMouseEvent* e) override {
        if (!dragging_ || e->button() != Qt::LeftButton) return;
        dragRect_  = QRect{dragStart_, e->pos()}.normalized();
        dragging_  = false;
        if (dragRect_.width() > 4 && dragRect_.height() > 4) {
            regionLocked_ = true;
            emit regionDrawn(dragRect_);
        }
        update();
    }

private:
    QImage image_;
    QPoint dragStart_;
    QRect dragRect_;
    bool dragging_;
    bool regionLocked_;
    int pageIndex_;
};

static mondoc::domain::FieldLocation regionToLocation(
    const QRect& rect, const QSize& imgSize, int pageIndex)
{
    mondoc::domain::PdfLocation loc;
    loc.page_index = pageIndex;
    loc.x = static_cast<double>(rect.x())      / imgSize.width();
    loc.y = static_cast<double>(rect.y())      / imgSize.height();
    loc.w = static_cast<double>(rect.width())  / imgSize.width();
    loc.h = static_cast<double>(rect.height()) / imgSize.height();
    return mondoc::domain::FieldLocation{loc, std::nullopt};
}

RegionMarkViewer::RegionMarkViewer(const std::filesystem::path& sourcePath,
                                   const QString& templateName,
                                   QWidget* parent)
    : QDialog(parent),
      sourcePath_(sourcePath),
      textBrowser_(new QTextBrowser(this)),
      pdfWidget_(nullptr),
      instructionLabel_(new QLabel(
          tr("Click and drag in the document to mark where this field appears."), this)),
      namePromptLabel_(new QLabel(tr("Name this field:"), this)),
      nameEdit_(new QLineEdit(this)),
      typePromptLabel_(new QLabel(tr("Field type:"), this)),
      typeCombo_(new QComboBox(this)),
      confirmRegionBtn_(new QPushButton(tr("Confirm Region"), this)),
      saveFieldBtn_(new QPushButton(tr("Save Field"), this)),
      closeBtn_(new QPushButton(tr("Close without Saving"), this))
{
    setWindowTitle(tr("Mark Field Region — %1").arg(templateName));
    setModal(true);
    setMinimumSize(700, 500);

    typeCombo_->addItem(tr("Text"),      static_cast<int>(mondoc::domain::FieldType::Text));
    typeCombo_->addItem(tr("Paragraph"), static_cast<int>(mondoc::domain::FieldType::Paragraph));
    typeCombo_->addItem(tr("Number"),    static_cast<int>(mondoc::domain::FieldType::Number));
    typeCombo_->addItem(tr("Date"),      static_cast<int>(mondoc::domain::FieldType::Date));
    typeCombo_->addItem(tr("Checkbox"),  static_cast<int>(mondoc::domain::FieldType::Checkbox));
    typeCombo_->addItem(tr("Dropdown"),  static_cast<int>(mondoc::domain::FieldType::Dropdown));

    nameEdit_->setAccessibleName(tr("Field name"));
    typeCombo_->setAccessibleName(tr("Field type"));
    confirmRegionBtn_->setAccessibleName(tr("Confirm region"));
    saveFieldBtn_->setAccessibleName(tr("Save field"));
    closeBtn_->setAccessibleName(tr("Close without saving"));

    confirmRegionBtn_->setStyleSheet(accentButtonStyle());
    saveFieldBtn_->setStyleSheet(accentButtonStyle());

    confirmRegionBtn_->setEnabled(false);
    saveFieldBtn_->setEnabled(false);

    namePromptLabel_->setVisible(false);
    nameEdit_->setVisible(false);
    typePromptLabel_->setVisible(false);
    typeCombo_->setVisible(false);
    saveFieldBtn_->setVisible(false);

    scrollArea_ = new QScrollArea(this);
    scrollArea_->setWidgetResizable(false);
    scrollArea_->setContentsMargins(0, 0, 0, 0);

    prevPageBtn_   = new QPushButton(tr("Previous Page"), this);
    nextPageBtn_   = new QPushButton(tr("Next Page"), this);
    pageIndicator_ = new QLabel(this);
    prevPageBtn_->setAccessibleName(tr("Previous page"));
    nextPageBtn_->setAccessibleName(tr("Next page"));
    connect(prevPageBtn_, &QPushButton::clicked, this, [this]() {
        if (pdfPageIndex_ > 0) renderPdfPage(pdfPageIndex_ - 1);
    });
    connect(nextPageBtn_, &QPushButton::clicked, this, [this]() {
        if (pdfDoc_ && pdfPageIndex_ + 1 < pdfDoc_->pageCount())
            renderPdfPage(pdfPageIndex_ + 1);
    });

    const bool isPdf = mondoc::hasExtension(sourcePath_, ".pdf");

    if (isPdf) {
        pdfWidget_ = new PdfPageWidget(this);
        scrollArea_->setWidget(pdfWidget_);
        if (!loadPdf(sourcePath_)) {
            instructionLabel_->setText(
                tr("MonDoc cannot display this document. "
                   "You can still mark a region by entering coordinates manually."));
        }
        connect(pdfWidget_, &PdfPageWidget::regionDrawn,
                this, &RegionMarkViewer::onRegionDrawn);
    } else {
        textBrowser_->setReadOnly(true);
        scrollArea_->setWidget(textBrowser_);
        const bool loaded = loadTextDocument(sourcePath_);
        confirmRegionBtn_->setText(tr("Mark Location"));
        confirmRegionBtn_->setEnabled(loaded);
        if (!loaded) {
            confirmRegionBtn_->setToolTip(
                tr("The document could not be read, so a location cannot be marked."));
        }
    }

    const bool multiPage = isPdf && pdfDoc_ && pdfDoc_->pageCount() > 1;
    prevPageBtn_->setVisible(multiPage);
    nextPageBtn_->setVisible(multiPage);
    pageIndicator_->setVisible(multiPage);

    auto* pageRow = new QHBoxLayout;
    pageRow->addWidget(prevPageBtn_);
    pageRow->addWidget(pageIndicator_);
    pageRow->addWidget(nextPageBtn_);
    pageRow->addStretch(1);

    auto* btnRow = new QHBoxLayout;
    btnRow->addStretch(1);
    btnRow->addWidget(closeBtn_);
    btnRow->addWidget(confirmRegionBtn_);
    btnRow->addWidget(saveFieldBtn_);

    auto* subStepRow = new QHBoxLayout;
    subStepRow->addWidget(namePromptLabel_);
    subStepRow->addWidget(nameEdit_);
    subStepRow->addSpacing(16);
    subStepRow->addWidget(typePromptLabel_);
    subStepRow->addWidget(typeCombo_);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 8, 0, 8);
    root->setSpacing(8);
    root->addWidget(instructionLabel_);
    root->addLayout(pageRow);
    root->addWidget(scrollArea_, 1);
    root->addLayout(subStepRow);
    root->addLayout(btnRow);

    connect(confirmRegionBtn_, &QPushButton::clicked, this, &RegionMarkViewer::onConfirmRegion);
    connect(saveFieldBtn_,     &QPushButton::clicked, this, &RegionMarkViewer::onSaveField);
    connect(closeBtn_,         &QPushButton::clicked, this, &RegionMarkViewer::onCloseWithoutSaving);
    connect(nameEdit_, &QLineEdit::textChanged, this, [this](const QString& text) {
        saveFieldBtn_->setEnabled(!text.trimmed().isEmpty());
    });
}

bool RegionMarkViewer::loadPdf(const std::filesystem::path& path) {
    pdfDoc_ = new QPdfDocument(this);
    const QString qpath = QString::fromStdU16String(path.u16string());
    if (pdfDoc_->load(qpath) != QPdfDocument::Error::None) return false;
    if (pdfDoc_->pageCount() == 0) return false;
    return renderPdfPage(0);
}

bool RegionMarkViewer::renderPdfPage(int pageIndex) {
    const QSizeF pagePts = pdfDoc_->pagePointSize(pageIndex);
    if (pagePts.width() <= 0.0 || pagePts.height() <= 0.0) return false;
    const int w = 800;
    const int h = static_cast<int>(800.0 * pagePts.height() / pagePts.width());
    const QImage img = pdfDoc_->render(pageIndex, QSize(w, h));
    if (img.isNull()) return false;
    pdfPageIndex_ = pageIndex;
    pdfWidget_->setImage(img, pageIndex);
    // A drawn region belongs to the page it was drawn on.
    regionDrawn_ = false;
    pendingLocation_ = std::nullopt;
    confirmRegionBtn_->setEnabled(false);
    pageIndicator_->setText(tr("Page %1 of %2")
        .arg(pageIndex + 1).arg(pdfDoc_->pageCount()));
    prevPageBtn_->setEnabled(pageIndex > 0);
    nextPageBtn_->setEnabled(pageIndex + 1 < pdfDoc_->pageCount());
    return true;
}

bool RegionMarkViewer::loadTextDocument(const std::filesystem::path& path) {
    std::string content;

    const bool needsExtraction = mondoc::hasExtension(path, ".docx") ||
                                  mondoc::hasExtension(path, ".odt") ||
                                  mondoc::hasExtension(path, ".pdf");

    if (needsExtraction) {
        auto extracted = mondoc::adapters::formats::extractPlainText(path);
        if (!extracted) {
            const QString errMsg = tr("Could not read document: %1")
                .arg(QString::fromStdString(extracted.error().message()));
            textBrowser_->setHtml(
                QStringLiteral("<html><body><p style=\"color:#B91C1C;font-weight:600;\">%1</p></body></html>")
                    .arg(errMsg.toHtmlEscaped()));
            return false;
        }
        content = *extracted;
    } else {
        std::ifstream f(path, std::ios::binary);
        if (!f) return false;
        std::ostringstream ss;
        ss << f.rdbuf();
        content = ss.str();
    }

    // Plain text, not HTML: toPlainText() must reproduce `content` byte-for-byte
    // so onConfirmRegion's offset math stays aligned with extractPlainText()'s
    // output. HTML rendering (previously used) collapses whitespace runs and
    // loses inter-paragraph spacing as a visual affordance in exchange.
    textBrowser_->setPlainText(QString::fromStdString(content));
    return true;
}

void RegionMarkViewer::onRegionDrawn(const QRect& rect) {
    regionDrawn_ = true;
    if (pdfWidget_) {
        pendingLocation_ = regionToLocation(rect, pdfWidget_->imageSize(),
                                            pdfWidget_->pageIndex());
    }
    confirmRegionBtn_->setEnabled(true);
}

void RegionMarkViewer::onConfirmRegion() {
    if (!pdfWidget_) {
        const QTextCursor cur = textBrowser_->textCursor();
        if (cur.hasSelection()) {
            const QString full = textBrowser_->toPlainText();
            const QString beforeStart = full.left(cur.selectionStart());
            const QString beforeEnd   = full.left(cur.selectionEnd());
            mondoc::domain::TextLocation tl;
            tl.char_offset     = static_cast<int>(beforeStart.toUtf8().size());
            tl.char_end        = static_cast<int>(beforeEnd.toUtf8().size());
            tl.paragraph_index = static_cast<int>(beforeStart.count(QLatin1Char('\n')));
            QString sel = full.mid(cur.selectionStart(),
                                   cur.selectionEnd() - cur.selectionStart());
            if (sel.size() > 120) sel = sel.left(120);
            tl.excerpt = sel.toStdString();
            pendingLocation_ = mondoc::domain::FieldLocation{std::nullopt, tl};
        } else {
            pendingLocation_ = std::nullopt;
        }
    }
    instructionLabel_->setVisible(false);
    namePromptLabel_->setVisible(true);
    nameEdit_->setVisible(true);
    typePromptLabel_->setVisible(true);
    typeCombo_->setVisible(true);
    saveFieldBtn_->setVisible(true);
    confirmRegionBtn_->setVisible(false);
    nameEdit_->setFocus();
}

void RegionMarkViewer::onSaveField() {
    mondoc::domain::Field f;
    f.name_     = nameEdit_->text().trimmed().toStdString();
    f.type_     = static_cast<mondoc::domain::FieldType>(typeCombo_->currentData().toInt());
    f.origin_   = mondoc::domain::FieldOrigin::Unknown;
    f.location_ = pendingLocation_;
    field_ = f;
    accept();
}

void RegionMarkViewer::onCloseWithoutSaving() {
    if (regionDrawn_) {
        QMessageBox box(this);
        box.setIcon(QMessageBox::Question);
        box.setWindowTitle(tr("Discard Region?"));
        box.setText(tr("Discard this region without saving the field?"));
        auto* discardBtn = box.addButton(tr("Discard"), QMessageBox::DestructiveRole);
        auto* keepBtn = box.addButton(tr("Keep Editing"), QMessageBox::RejectRole);
        box.setDefaultButton(keepBtn);
        box.exec();
        if (box.clickedButton() != discardBtn) return;
    }
    reject();
}

mondoc::domain::Field RegionMarkViewer::field() const {
    return field_;
}

}  // namespace mondoc::ui

#include "region_mark_viewer.moc"
