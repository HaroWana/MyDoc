#include "document_canvas.hpp"

#include "canvas_geometry.hpp"

#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QPdfDocument>
#include <QVBoxLayout>
#include <QLabel>
#include <QImage>
#include <QColor>
#include <QPen>

#include <algorithm>
#include <array>
#include <utility>

namespace mondoc::ui {

namespace {
constexpr int kHandlePx = 6;
constexpr int kFillAlpha = 38;  // ~15% of 255

struct FrameEntry {
    mondoc::FieldId id;
    PixelRect rect;
};

PixelRect clampMove(const PixelRect& orig, int dx, int dy, int maxW, int maxH) {
    const int hiX = std::max(0, maxW - orig.w);
    const int hiY = std::max(0, maxH - orig.h);
    return PixelRect{std::clamp(orig.x + dx, 0, hiX),
                      std::clamp(orig.y + dy, 0, hiY),
                      orig.w, orig.h};
}

PixelRect applyResize(const PixelRect& orig, HitZone zone, int dx, int dy, int maxW, int maxH) {
    int x1 = orig.x, y1 = orig.y, x2 = orig.x + orig.w, y2 = orig.y + orig.h;
    switch (zone) {
        case HitZone::TopLeft:     x1 += dx; y1 += dy; break;
        case HitZone::Top:         y1 += dy; break;
        case HitZone::TopRight:    x2 += dx; y1 += dy; break;
        case HitZone::Right:       x2 += dx; break;
        case HitZone::BottomRight: x2 += dx; y2 += dy; break;
        case HitZone::Bottom:      y2 += dy; break;
        case HitZone::BottomLeft:  x1 += dx; y2 += dy; break;
        case HitZone::Left:        x1 += dx; break;
        default: break;
    }
    x1 = std::clamp(x1, 0, maxW);
    x2 = std::clamp(x2, 0, maxW);
    y1 = std::clamp(y1, 0, maxH);
    y2 = std::clamp(y2, 0, maxH);
    if (x2 < x1) std::swap(x1, x2);
    if (y2 < y1) std::swap(y1, y2);
    return PixelRect{x1, y1, x2 - x1, y2 - y1};
}
}  // namespace

class CanvasPageWidget : public QWidget {
    Q_OBJECT
public:
    explicit CanvasPageWidget(int pageIndex, QWidget* parent = nullptr)
        : QWidget(parent), page_index_(pageIndex) {
        setCursor(Qt::CrossCursor);
    }

    void setImage(const QImage& img) {
        image_ = img;
        setFixedSize(image_.size());
        frames_.clear();
        mode_ = Mode::None;
        active_id_.reset();
        pending_draw_rect_ = PixelRect{};
        update();
    }

    QSize imageSize() const { return image_.size(); }
    int pageIndex() const { return page_index_; }

    void setFrames(std::vector<FrameEntry> frames) {
        frames_ = std::move(frames);
        // A frame list swapped in mid-drag would leave active_id_ dangling.
        mode_ = Mode::None;
        active_id_.reset();
        update();
    }

    void setSelected(std::optional<mondoc::FieldId> id) {
        selected_ = id;
        update();
    }

signals:
    void frameDrawn(PixelRect rect);
    void frameSelected(mondoc::FieldId id);
    void frameChanged(mondoc::FieldId id, PixelRect rect);

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        if (!image_.isNull()) p.drawImage(0, 0, image_);

        const QColor accent(0x25, 0x63, 0xEB);
        for (const auto& f : frames_) {
            const bool isSelected = selected_ && f.id == *selected_;
            const QRect r(f.rect.x, f.rect.y, f.rect.w, f.rect.h);
            p.fillRect(r, QColor(0x25, 0x63, 0xEB, kFillAlpha));
            p.setPen(QPen(accent, isSelected ? 3 : 2));
            p.drawRect(r);
            if (isSelected) drawHandles(p, f.rect);
        }

        if (mode_ == Mode::Draw) {
            const QRect r(pending_draw_rect_.x, pending_draw_rect_.y,
                          pending_draw_rect_.w, pending_draw_rect_.h);
            p.fillRect(r, QColor(0x25, 0x63, 0xEB, kFillAlpha));
            p.setPen(QPen(accent, 2));
            p.drawRect(r);
        }
    }

    void mousePressEvent(QMouseEvent* e) override {
        if (e->button() != Qt::LeftButton) return;
        const QPoint pos = e->pos();

        FrameEntry* hit = nullptr;
        HitZone zone = HitZone::None;

        if (selected_) {
            if (auto* f = findFrame(*selected_)) {
                const HitZone z = hitTest(f->rect, pos.x(), pos.y(), kHandlePx);
                if (z != HitZone::None) { hit = f; zone = z; }
            }
        }
        if (!hit) {
            for (auto& f : frames_) {
                if (selected_ && f.id == *selected_) continue;
                if (hitTest(f.rect, pos.x(), pos.y(), 0) == HitZone::Inside) {
                    hit = &f;
                    zone = HitZone::Inside;
                    break;
                }
            }
        }

        if (hit) {
            active_id_ = hit->id;
            drag_start_ = pos;
            drag_orig_rect_ = hit->rect;
            if (zone == HitZone::Inside) {
                mode_ = Mode::Move;
                emit frameSelected(hit->id);
            } else {
                mode_ = Mode::Resize;
                resize_zone_ = zone;
            }
        } else {
            mode_ = Mode::Draw;
            drag_start_ = pos;
            pending_draw_rect_ = PixelRect{pos.x(), pos.y(), 0, 0};
        }
        update();
    }

    void mouseMoveEvent(QMouseEvent* e) override {
        if (mode_ == Mode::None) return;
        const QPoint pos = e->pos();
        const int dx = pos.x() - drag_start_.x();
        const int dy = pos.y() - drag_start_.y();
        const int maxW = image_.width();
        const int maxH = image_.height();

        switch (mode_) {
            case Mode::Draw: {
                const QRect r = QRect(drag_start_, pos).normalized();
                pending_draw_rect_ = PixelRect{r.x(), r.y(), r.width(), r.height()};
                break;
            }
            case Mode::Move:
                if (auto* f = findFrame(*active_id_))
                    f->rect = clampMove(drag_orig_rect_, dx, dy, maxW, maxH);
                break;
            case Mode::Resize:
                if (auto* f = findFrame(*active_id_))
                    f->rect = applyResize(drag_orig_rect_, resize_zone_, dx, dy, maxW, maxH);
                break;
            default: break;
        }
        update();
    }

    void mouseReleaseEvent(QMouseEvent* e) override {
        if (e->button() != Qt::LeftButton || mode_ == Mode::None) return;
        const Mode finishedMode = mode_;
        mode_ = Mode::None;

        if (finishedMode == Mode::Draw) {
            const PixelRect r = pending_draw_rect_;
            pending_draw_rect_ = PixelRect{};
            if (r.w > 4 && r.h > 4) emit frameDrawn(r);
        } else if (active_id_) {
            if (auto* f = findFrame(*active_id_)) emit frameChanged(*active_id_, f->rect);
        }
        active_id_.reset();
        update();
    }

private:
    enum class Mode { None, Draw, Move, Resize };

    FrameEntry* findFrame(const mondoc::FieldId& id) {
        for (auto& f : frames_)
            if (f.id == id) return &f;
        return nullptr;
    }

    static void drawHandles(QPainter& p, const PixelRect& r) {
        const int half = kHandlePx / 2;
        const std::array<QPoint, 8> points = {
            QPoint(r.x, r.y),                 QPoint(r.x + r.w / 2, r.y),
            QPoint(r.x + r.w, r.y),           QPoint(r.x + r.w, r.y + r.h / 2),
            QPoint(r.x + r.w, r.y + r.h),     QPoint(r.x + r.w / 2, r.y + r.h),
            QPoint(r.x, r.y + r.h),           QPoint(r.x, r.y + r.h / 2),
        };
        p.setPen(QPen(QColor(0x25, 0x63, 0xEB), 1));
        p.setBrush(QColor(255, 255, 255));
        for (const auto& pt : points)
            p.drawRect(pt.x() - half, pt.y() - half, kHandlePx, kHandlePx);
    }

    QImage image_;
    int page_index_;
    std::vector<FrameEntry> frames_;
    std::optional<mondoc::FieldId> selected_;

    Mode mode_ = Mode::None;
    QPoint drag_start_;
    PixelRect drag_orig_rect_{};
    HitZone resize_zone_ = HitZone::None;
    std::optional<mondoc::FieldId> active_id_;
    PixelRect pending_draw_rect_{};
};

DocumentCanvas::DocumentCanvas(QWidget* parent)
    : QScrollArea(parent),
      container_(new QWidget(this)),
      layout_(new QVBoxLayout(container_)),
      warning_label_(new QLabel(
          tr("This preview may not reflect recent document changes."), container_))
{
    warning_label_->setStyleSheet(
        QStringLiteral("QLabel { background-color: #FEF3C7; color: #92400E; padding: 6px; }"));
    warning_label_->setAlignment(Qt::AlignCenter);
    warning_label_->setVisible(false);

    layout_->setContentsMargins(0, 0, 0, 0);
    layout_->setSpacing(8);
    layout_->addWidget(warning_label_);
    layout_->addStretch(1);

    setWidgetResizable(true);
    setWidget(container_);
}

bool DocumentCanvas::loadDocument(const std::filesystem::path& previewPdf) {
    clearDocument();
    document_ = new QPdfDocument(this);
    const QString qpath = QString::fromStdU16String(previewPdf.u16string());
    if (document_->load(qpath) != QPdfDocument::Error::None) {
        delete document_;
        document_ = nullptr;
        return false;
    }
    if (document_->pageCount() == 0) {
        delete document_;
        document_ = nullptr;
        return false;
    }
    rebuildPages();
    return true;
}

void DocumentCanvas::clearDocument() {
    for (auto* page : pages_) {
        layout_->removeWidget(page);
        page->deleteLater();
    }
    pages_.clear();
    fields_.clear();
    selected_id_.reset();
    if (document_) {
        delete document_;
        document_ = nullptr;
    }
}

void DocumentCanvas::setFrames(const std::vector<mondoc::domain::Field>& fields) {
    fields_ = fields;
    applyFrames();
}

void DocumentCanvas::setSelectedField(const mondoc::FieldId& id) {
    selected_id_ = id;
    for (auto* page : pages_) page->setSelected(selected_id_);
}

void DocumentCanvas::setStaleWarning(bool visible) {
    warning_label_->setVisible(visible);
}

void DocumentCanvas::wheelEvent(QWheelEvent* event) {
    if (event->modifiers() & Qt::ControlModifier) {
        const double step = (event->angleDelta().y() > 0) ? 0.1 : -0.1;
        const double newZoom = std::clamp(zoom_ + step, 0.5, 3.0);
        if (newZoom != zoom_) {
            zoom_ = newZoom;
            rerenderPages();
        }
        event->accept();
        return;
    }
    QScrollArea::wheelEvent(event);
}

void DocumentCanvas::rebuildPages() {
    for (auto* page : pages_) {
        layout_->removeWidget(page);
        page->deleteLater();
    }
    pages_.clear();

    if (!document_) return;

    const int pageCount = document_->pageCount();
    for (int i = 0; i < pageCount; ++i) {
        auto* page = new CanvasPageWidget(i, container_);
        layout_->insertWidget(layout_->count() - 1, page);

        connect(page, &CanvasPageWidget::frameSelected, this, &DocumentCanvas::frameSelected);
        connect(page, &CanvasPageWidget::frameDrawn, this, [this, i, page](PixelRect r) {
            emit frameDrawn(toNormalized(i, r, page->imageSize().width(), page->imageSize().height()));
        });
        connect(page, &CanvasPageWidget::frameChanged, this,
                [this, i, page](mondoc::FieldId id, PixelRect r) {
            emit frameChanged(id, toNormalized(i, r, page->imageSize().width(),
                                                page->imageSize().height()));
        });

        pages_.push_back(page);
    }
    rerenderPages();
}

void DocumentCanvas::rerenderPages() {
    if (!document_) return;
    for (auto* page : pages_) {
        const int idx = page->pageIndex();
        const QSizeF pagePts = document_->pagePointSize(idx);
        const int w = static_cast<int>(kPageWidthPx * zoom_);
        const int h = (pagePts.width() > 0.0)
            ? static_cast<int>(w * pagePts.height() / pagePts.width())
            : w;
        page->setImage(document_->render(idx, QSize(w, h)));
    }
    applyFrames();
}

void DocumentCanvas::applyFrames() {
    std::vector<std::vector<FrameEntry>> byPage(pages_.size());
    for (const auto& field : fields_) {
        if (!field.location_ || !field.location_->pdf) continue;
        const auto& loc = *field.location_->pdf;
        if (loc.page_index < 0 || static_cast<std::size_t>(loc.page_index) >= pages_.size())
            continue;
        auto* page = pages_[static_cast<std::size_t>(loc.page_index)];
        const PixelRect rect = toPixels(loc, page->imageSize().width(), page->imageSize().height());
        byPage[static_cast<std::size_t>(loc.page_index)].push_back(FrameEntry{field.id_, rect});
    }
    for (std::size_t i = 0; i < pages_.size(); ++i) {
        pages_[i]->setFrames(std::move(byPage[i]));
        pages_[i]->setSelected(selected_id_);
    }
}

}  // namespace mondoc::ui

#include "document_canvas.moc"
