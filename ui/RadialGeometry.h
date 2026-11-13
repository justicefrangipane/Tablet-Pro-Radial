#pragma once

#include <QPainterPath>
#include <QPainterPathStroker>
#include <QRegion>
#include <cmath>

namespace tabletpro::radialGeometry {
inline QPainterPath wedge(double angle, double span) {
    constexpr double inner = 44, outer = 96;
    const auto a0 = angle-span/2+.035, a1 = angle+span/2-.035;
    const auto point = [](double r, double a) { return QPointF(r*std::cos(a), r*std::sin(a)); };
    QPainterPath path;
    path.moveTo(point(outer,a0));
    for(int i=1;i<=32;++i)path.lineTo(point(outer,a0+(a1-a0)*i/32));
    path.lineTo(point(inner,a1));
    for(int i=1;i<=32;++i)path.lineTo(point(inner,a1+(a0-a1)*i/32));
    path.closeSubpath();
    return path;
}

inline QRegion nativeWedge(QPointF center, double angle) {
    // The native window's integer cutout must contain the painted stroke,
    // antialiasing and rounding at fractional centers/display scales. Angular
    // padding alone becomes too narrow at the wedge's inner corners.
    const auto path=wedge(angle,3.141592653589793/3).translated(center);
    QPainterPathStroker guard;
    guard.setWidth(5);
    guard.setJoinStyle(Qt::RoundJoin);
    return QRegion(path.united(guard.createStroke(path)).toFillPolygon().toPolygon(),Qt::WindingFill);
}
}
