#include "region_mark_viewer.hpp"

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

#include "region_mark_viewer.moc"
