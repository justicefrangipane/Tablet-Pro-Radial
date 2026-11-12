#pragma once
#include <QRectF>

namespace tabletpro::DockLayout {
inline constexpr qreal Width = 44;
inline constexpr qreal Height = 347;
inline constexpr qreal RowStep = 51;
inline constexpr qreal FirstCenter = 66;

// The assembly guide, landing positions and finished housing share one layout.
inline QRectF bounds(qreal viewportHeight) {
    return {13, viewportHeight/2 - FirstCenter - 2.5*RowStep, Width, Height};
}
inline QPointF buttonCenter(int slot, qreal viewportHeight) {
    return bounds(viewportHeight).topLeft() + QPointF(Width/2, FirstCenter + slot*RowStep);
}
inline QRectF fromButton(QPointF center, int slot) {
    return {center - QPointF(Width/2, FirstCenter + slot*RowStep), QSizeF(Width, Height)};
}
}
