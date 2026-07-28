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
        : QWidget(parent), dragging_(false), region_locked_(false), page_index_(0)
    {
        setCursor(Qt::CrossCursor);
        setMouseTracking(false);
    }

    void setImage(const QImage& img, int pageIndex) {
        image_     = img;
        page_index_ = pageIndex;
        drag_rect_  = QRect{};
        dragging_  = false;
        region_locked_ = false;
        setFixedSize(image_.size());
        update();
    }

    QRect dragRect() const { return drag_rect_; }
    QSize imageSize() const { return image_.size(); }
    int pageIndex() const { return page_index_; }
    bool hasRegion() const { return region_locked_; }

signals:
    void regionDrawn(const QRect& rect);

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        if (!image_.isNull())
            p.drawImage(0, 0, image_);
        if (dragging_ || region_locked_) {
            QColor fillColor(0x25, 0x63, 0xEB, 64);
            p.fillRect(drag_rect_.normalized(), fillColor);
            p.setPen(QPen(QColor(0x25, 0x63, 0xEB), 2));
            p.drawRect(drag_rect_.normalized());
        }
    }

    void mousePressEvent(QMouseEvent* e) override {
        if (e->button() != Qt::LeftButton) return;
        drag_start_ = e->pos();
        drag_rect_  = QRect{drag_start_, drag_start_};
        dragging_  = true;
        region_locked_ = false;
        update();
    }

    void mouseMoveEvent(QMouseEvent* e) override {
        if (!dragging_) return;
        drag_rect_ = QRect{drag_start_, e->pos()}.normalized();
        update();
    }

    void mouseReleaseEvent(QMouseEvent* e) override {
        if (!dragging_ || e->button() != Qt::LeftButton) return;
        drag_rect_  = QRect{drag_start_, e->pos()}.normalized();
        dragging_  = false;
        if (drag_rect_.width() > 4 && drag_rect_.height() > 4) {
            region_locked_ = true;
            emit regionDrawn(drag_rect_);
        }
        update();
    }

private:
    QImage image_;
    QPoint drag_start_;
    QRect drag_rect_;
    bool dragging_;
    bool region_locked_;
    int page_index_;
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
      source_path_(sourcePath),
      text_browser_(new QTextBrowser(this)),
      pdf_widget_(nullptr),
      instruction_label_(new QLabel(
          tr("Click and drag in the document to mark where this field appears."), this)),
      name_prompt_label_(new QLabel(tr("Name this field:"), this)),
      name_edit_(new QLineEdit(this)),
      type_prompt_label_(new QLabel(tr("Field type:"), this)),
      type_combo_(new QComboBox(this)),
      confirm_region_btn_(new QPushButton(tr("Confirm Region"), this)),
      save_field_btn_(new QPushButton(tr("Save Field"), this)),
      close_btn_(new QPushButton(tr("Close without Saving"), this))
{
    setWindowTitle(tr("Mark Field Region — %1").arg(templateName));
    setModal(true);
    setMinimumSize(700, 500);

    type_combo_->addItem(tr("Text"),      static_cast<int>(mondoc::domain::FieldType::Text));
    type_combo_->addItem(tr("Paragraph"), static_cast<int>(mondoc::domain::FieldType::Paragraph));
    type_combo_->addItem(tr("Number"),    static_cast<int>(mondoc::domain::FieldType::Number));
    type_combo_->addItem(tr("Date"),      static_cast<int>(mondoc::domain::FieldType::Date));
    type_combo_->addItem(tr("Checkbox"),  static_cast<int>(mondoc::domain::FieldType::Checkbox));
    type_combo_->addItem(tr("Dropdown"),  static_cast<int>(mondoc::domain::FieldType::Dropdown));

    name_edit_->setAccessibleName(tr("Field name"));
    type_combo_->setAccessibleName(tr("Field type"));
    confirm_region_btn_->setAccessibleName(tr("Confirm region"));
    save_field_btn_->setAccessibleName(tr("Save field"));
    close_btn_->setAccessibleName(tr("Close without saving"));

    confirm_region_btn_->setStyleSheet(accentButtonStyle());
    save_field_btn_->setStyleSheet(accentButtonStyle());

    confirm_region_btn_->setEnabled(false);
    save_field_btn_->setEnabled(false);

    name_prompt_label_->setVisible(false);
    name_edit_->setVisible(false);
    type_prompt_label_->setVisible(false);
    type_combo_->setVisible(false);
    save_field_btn_->setVisible(false);

    scroll_area_ = new QScrollArea(this);
    scroll_area_->setWidgetResizable(false);
    scroll_area_->setContentsMargins(0, 0, 0, 0);

    prev_page_btn_   = new QPushButton(tr("Previous Page"), this);
    next_page_btn_   = new QPushButton(tr("Next Page"), this);
    page_indicator_ = new QLabel(this);
    prev_page_btn_->setAccessibleName(tr("Previous page"));
    next_page_btn_->setAccessibleName(tr("Next page"));
    connect(prev_page_btn_, &QPushButton::clicked, this, [this]() {
        if (pdf_page_index_ > 0) renderPdfPage(pdf_page_index_ - 1);
    });
    connect(next_page_btn_, &QPushButton::clicked, this, [this]() {
        if (pdf_doc_ && pdf_page_index_ + 1 < pdf_doc_->pageCount())
            renderPdfPage(pdf_page_index_ + 1);
    });

    const bool isPdf = mondoc::hasExtension(source_path_, ".pdf");

    if (isPdf) {
        pdf_widget_ = new PdfPageWidget(this);
        scroll_area_->setWidget(pdf_widget_);
        if (!loadPdf(source_path_)) {
            instruction_label_->setText(
                tr("MonDoc cannot display this document. "
                   "You can still mark a region by entering coordinates manually."));
        }
        connect(pdf_widget_, &PdfPageWidget::regionDrawn,
                this, &RegionMarkViewer::onRegionDrawn);
    } else {
        text_browser_->setReadOnly(true);
        scroll_area_->setWidget(text_browser_);
        const bool loaded = loadTextDocument(source_path_);
        confirm_region_btn_->setText(tr("Mark Location"));
        confirm_region_btn_->setEnabled(loaded);
        if (!loaded) {
            confirm_region_btn_->setToolTip(
                tr("The document could not be read, so a location cannot be marked."));
        }
    }

    const bool multiPage = isPdf && pdf_doc_ && pdf_doc_->pageCount() > 1;
    prev_page_btn_->setVisible(multiPage);
    next_page_btn_->setVisible(multiPage);
    page_indicator_->setVisible(multiPage);

    auto* pageRow = new QHBoxLayout;
    pageRow->addWidget(prev_page_btn_);
    pageRow->addWidget(page_indicator_);
    pageRow->addWidget(next_page_btn_);
    pageRow->addStretch(1);

    auto* btnRow = new QHBoxLayout;
    btnRow->addStretch(1);
    btnRow->addWidget(close_btn_);
    btnRow->addWidget(confirm_region_btn_);
    btnRow->addWidget(save_field_btn_);

    auto* subStepRow = new QHBoxLayout;
    subStepRow->addWidget(name_prompt_label_);
    subStepRow->addWidget(name_edit_);
    subStepRow->addSpacing(16);
    subStepRow->addWidget(type_prompt_label_);
    subStepRow->addWidget(type_combo_);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 8, 0, 8);
    root->setSpacing(8);
    root->addWidget(instruction_label_);
    root->addLayout(pageRow);
    root->addWidget(scroll_area_, 1);
    root->addLayout(subStepRow);
    root->addLayout(btnRow);

    connect(confirm_region_btn_, &QPushButton::clicked, this, &RegionMarkViewer::onConfirmRegion);
    connect(save_field_btn_,     &QPushButton::clicked, this, &RegionMarkViewer::onSaveField);
    connect(close_btn_,         &QPushButton::clicked, this, &RegionMarkViewer::onCloseWithoutSaving);
    connect(name_edit_, &QLineEdit::textChanged, this, [this](const QString& text) {
        save_field_btn_->setEnabled(!text.trimmed().isEmpty());
    });
}

bool RegionMarkViewer::loadPdf(const std::filesystem::path& path) {
    pdf_doc_ = new QPdfDocument(this);
    const QString qpath = QString::fromStdU16String(path.u16string());
    if (pdf_doc_->load(qpath) != QPdfDocument::Error::None) return false;
    if (pdf_doc_->pageCount() == 0) return false;
    return renderPdfPage(0);
}

bool RegionMarkViewer::renderPdfPage(int pageIndex) {
    const QSizeF pagePts = pdf_doc_->pagePointSize(pageIndex);
    if (pagePts.width() <= 0.0 || pagePts.height() <= 0.0) return false;
    const int w = 800;
    const int h = static_cast<int>(800.0 * pagePts.height() / pagePts.width());
    const QImage img = pdf_doc_->render(pageIndex, QSize(w, h));
    if (img.isNull()) return false;
    pdf_page_index_ = pageIndex;
    pdf_widget_->setImage(img, pageIndex);
    // A drawn region belongs to the page it was drawn on.
    region_drawn_ = false;
    pending_location_ = std::nullopt;
    confirm_region_btn_->setEnabled(false);
    page_indicator_->setText(tr("Page %1 of %2")
        .arg(pageIndex + 1).arg(pdf_doc_->pageCount()));
    prev_page_btn_->setEnabled(pageIndex > 0);
    next_page_btn_->setEnabled(pageIndex + 1 < pdf_doc_->pageCount());
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
            text_browser_->setHtml(
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
    text_browser_->setPlainText(QString::fromStdString(content));
    return true;
}

void RegionMarkViewer::onRegionDrawn(const QRect& rect) {
    region_drawn_ = true;
    if (pdf_widget_) {
        pending_location_ = regionToLocation(rect, pdf_widget_->imageSize(),
                                            pdf_widget_->pageIndex());
    }
    confirm_region_btn_->setEnabled(true);
}

void RegionMarkViewer::onConfirmRegion() {
    if (!pdf_widget_) {
        const QTextCursor cur = text_browser_->textCursor();
        if (cur.hasSelection()) {
            const QString full = text_browser_->toPlainText();
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
            pending_location_ = mondoc::domain::FieldLocation{std::nullopt, tl};
        } else {
            pending_location_ = std::nullopt;
        }
    }
    instruction_label_->setVisible(false);
    name_prompt_label_->setVisible(true);
    name_edit_->setVisible(true);
    type_prompt_label_->setVisible(true);
    type_combo_->setVisible(true);
    save_field_btn_->setVisible(true);
    confirm_region_btn_->setVisible(false);
    name_edit_->setFocus();
}

void RegionMarkViewer::onSaveField() {
    mondoc::domain::Field f;
    f.name_     = name_edit_->text().trimmed().toStdString();
    f.type_     = static_cast<mondoc::domain::FieldType>(type_combo_->currentData().toInt());
    f.origin_   = mondoc::domain::FieldOrigin::Unknown;
    f.location_ = pending_location_;
    field_ = f;
    accept();
}

void RegionMarkViewer::onCloseWithoutSaving() {
    if (region_drawn_) {
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
