// SPDX-License-Identifier: AGPL-3.0-only
#include "measure_overlay.hpp"

#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>

namespace fos::viewport::detail {
namespace {

const QColor kLineColor(20, 90, 200);
const QColor kSnapColor(220, 30, 120);

std::optional<QPointF> toPixel(const Camera& camera, geometry::Vec3 p) {
    const auto px = camera.projectToPixel(p);
    if (!px) return std::nullopt;
    return QPointF(px->x, px->y);
}

void drawMarker(QPainter& painter, QPointF c, SnapKind kind) {
    constexpr double r = 5.0;
    switch (kind) {
    case SnapKind::Corner: painter.drawRect(QRectF(c.x() - r, c.y() - r, 2 * r, 2 * r)); break;
    case SnapKind::OutlineVertex: {
        const QPointF pts[4] = {{c.x(), c.y() - r}, {c.x() + r, c.y()}, {c.x(), c.y() + r}, {c.x() - r, c.y()}};
        painter.drawPolygon(pts, 4);
        break;
    }
    case SnapKind::EdgeMidpoint: {
        const QPointF pts[3] = {{c.x(), c.y() - r}, {c.x() + r, c.y() + r}, {c.x() - r, c.y() + r}};
        painter.drawPolygon(pts, 3);
        break;
    }
    case SnapKind::FaceCenter: painter.drawEllipse(c, r, r); break;
    case SnapKind::Surface:
        painter.drawLine(QPointF(c.x() - r, c.y()), QPointF(c.x() + r, c.y()));
        painter.drawLine(QPointF(c.x(), c.y() - r), QPointF(c.x(), c.y() + r));
        break;
    }
}

} // namespace

QString distanceLabel(double mm, const QString& suffix) { return QString::number(mm, 'f', 1) + suffix; }

void paintMeasureOverlay(QPainter& painter, const Camera& camera, const std::vector<SnapPoint>& points,
                         const std::optional<SnapPoint>& hover, const QString& lengthSuffix) {
    painter.setRenderHint(QPainter::Antialiasing);
    // Second end of the line: the second point, or the hover snap while picking it.
    std::optional<geometry::Vec3> a, b;
    if (!points.empty()) a = points[0].point;
    if (points.size() >= 2)
        b = points[1].point;
    else if (points.size() == 1 && hover)
        b = hover->point;

    if (a && b) {
        const auto pa = toPixel(camera, *a), pb = toPixel(camera, *b);
        if (pa && pb) {
            painter.setPen(QPen(kLineColor, 2.0, points.size() >= 2 ? Qt::SolidLine : Qt::DashLine));
            painter.drawLine(*pa, *pb);
            const QString text = distanceLabel(Measurement{*a, *b}.distance(), lengthSuffix);
            const QFontMetrics fm(painter.font());
            QRectF box = fm.boundingRect(text).toRectF().adjusted(-4, -2, 4, 2);
            box.moveCenter((*pa + *pb) / 2.0 + QPointF(0, -14));
            painter.setPen(QPen(kLineColor, 1.0));
            painter.setBrush(QColor(255, 255, 255, 230));
            painter.drawRoundedRect(box, 3, 3);
            painter.setPen(QColor(20, 20, 30));
            painter.drawText(box, Qt::AlignCenter, text);
        }
    }
    painter.setPen(QPen(kLineColor, 1.5));
    painter.setBrush(kLineColor);
    for (const SnapPoint& p : points)
        if (const auto px = toPixel(camera, p.point)) painter.drawEllipse(*px, 3.5, 3.5);
    if (hover) {
        if (const auto px = toPixel(camera, hover->point)) {
            painter.setPen(QPen(kSnapColor, 2.0));
            painter.setBrush(Qt::NoBrush);
            drawMarker(painter, *px, hover->kind);
        }
    }
}

} // namespace fos::viewport::detail
