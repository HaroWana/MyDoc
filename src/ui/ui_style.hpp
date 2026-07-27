#pragma once

#include <QString>

namespace mondoc::ui {

inline QString accentColor() {
    return QStringLiteral("#2563EB");
}

inline QString accentButtonStyle() {
    return QStringLiteral("QPushButton { background-color: %1; color: white; "
                          "padding: 6px 12px; }").arg(accentColor());
}

}  // namespace mondoc::ui
