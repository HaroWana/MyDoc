#pragma once
#include <cmath>
#include "domain/field.hpp"

namespace mondoc::ui {

struct PixelRect { int x = 0, y = 0, w = 0, h = 0; };

enum class HitZone { None, Inside,
    TopLeft, Top, TopRight, Right, BottomRight, Bottom, BottomLeft, Left };

inline PixelRect toPixels(const mondoc::domain::PdfLocation& loc,
                          int pageWpx, int pageHpx) {
    return {static_cast<int>(loc.x * pageWpx), static_cast<int>(loc.y * pageHpx),
            static_cast<int>(loc.w * pageWpx), static_cast<int>(loc.h * pageHpx)};
}

inline mondoc::domain::PdfLocation toNormalized(int pageIndex, const PixelRect& r,
                                                int pageWpx, int pageHpx) {
    mondoc::domain::PdfLocation loc;
    loc.page_index = pageIndex;
    loc.x = static_cast<double>(r.x) / pageWpx;
    loc.y = static_cast<double>(r.y) / pageHpx;
    loc.w = static_cast<double>(r.w) / pageWpx;
    loc.h = static_cast<double>(r.h) / pageHpx;
    return loc;
}

inline HitZone hitTest(const PixelRect& frame, int px, int py, int handlePx) {
    int x1 = frame.x;
    int y1 = frame.y;
    int x2 = frame.x + frame.w;
    int y2 = frame.y + frame.h;

    // Check if point is outside bounds
    if (px < x1 - handlePx || px > x2 + handlePx ||
        py < y1 - handlePx || py > y2 + handlePx) {
        return HitZone::None;
    }

    // Determine if we're near each edge
    bool nearLeft = px >= x1 - handlePx && px < x1 + handlePx;
    bool nearRight = px > x2 - handlePx && px <= x2 + handlePx;
    bool nearTop = py >= y1 - handlePx && py < y1 + handlePx;
    bool nearBottom = py > y2 - handlePx && py <= y2 + handlePx;

    // Determine if we're inside the frame (excluding handle zones)
    bool insideHorizontal = px >= x1 + handlePx && px <= x2 - handlePx;
    bool insideVertical = py >= y1 + handlePx && py <= y2 - handlePx;
    bool fullyInside = insideHorizontal && insideVertical;

    // Check corners first
    if (nearLeft && nearTop) return HitZone::TopLeft;
    if (nearRight && nearTop) return HitZone::TopRight;
    if (nearLeft && nearBottom) return HitZone::BottomLeft;
    if (nearRight && nearBottom) return HitZone::BottomRight;

    // Check edges (extend to full interior span on other axis)
    if (nearTop && py <= y2 + handlePx) return HitZone::Top;
    if (nearBottom && py >= y1 - handlePx) return HitZone::Bottom;
    if (nearLeft && px <= x2 + handlePx) return HitZone::Left;
    if (nearRight && px >= x1 - handlePx) return HitZone::Right;

    // Check interior
    if (fullyInside) return HitZone::Inside;

    return HitZone::None;
}

}  // namespace mondoc::ui
