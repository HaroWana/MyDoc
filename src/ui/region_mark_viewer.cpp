#include "region_mark_viewer.hpp"

#include "mondoc/util.hpp"

#include <QPainter>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QScrollArea>
#include <QPdfDocument>
#include <QTextBrowser>
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
        loadTextDocument(sourcePath_);
        confirmRegionBtn_->setText(tr("Mark Location"));
        confirmRegionBtn_->setEnabled(true);
    }

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
    auto* doc = new QPdfDocument(this);
    const QString qpath = QString::fromStdU16String(path.u16string());
    if (doc->load(qpath) != QPdfDocument::Error::None) return false;
    if (doc->pageCount() == 0) return false;
    const QSizeF pagePts = doc->pagePointSize(0);
    if (pagePts.width() <= 0.0 || pagePts.height() <= 0.0) return false;
    const int w = 800;
    const int h = static_cast<int>(800.0 * pagePts.height() / pagePts.width());
    const QImage img = doc->render(0, QSize(w, h));
    if (img.isNull()) return false;
    pdfWidget_->setImage(img, 0);
    return true;
}

bool RegionMarkViewer::loadTextDocument(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    const std::string content = ss.str();

    std::string html = "<html><body>";
    std::istringstream lines(content);
    std::string line;
    while (std::getline(lines, line)) {
        std::string escaped;
        for (char c : line) {
            if (c == '<') escaped += "&lt;";
            else if (c == '>') escaped += "&gt;";
            else if (c == '&') escaped += "&amp;";
            else escaped += c;
        }
        html += "<p>" + escaped + "</p>";
    }
    html += "</body></html>";
    textBrowser_->setHtml(QString::fromStdString(html));
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
        mondoc::domain::TextLocation tl;
        tl.paragraph_index = 0;
        tl.char_offset = 0;
        pendingLocation_ = mondoc::domain::FieldLocation{std::nullopt, tl};
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
