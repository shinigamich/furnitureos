// SPDX-License-Identifier: AGPL-3.0-only
#include "handle_overlay.hpp"

#include <QFontMetrics>
#include <QPainter>
#include <QRadialGradient>

#include <cmath>

namespace fos::viewport::detail {
namespace {

const QColor kAxisColors[3] = {QColor(210, 50, 50), QColor(40, 160, 60), QColor(40, 90, 210)};
const QColor kGhostColor(240, 140, 0);
constexpr double kArrowPx = 22.0;

std::optional<QPointF> toPixel(const Camera& camera, geometry::Vec3 p) {
    const auto px = camera.projectToPixel(p);
    if (!px) return std::nullopt;
    return QPointF(px->x, px->y);
}

void drawGhost(QPainter& painter, const Camera& camera, const ResizeTarget& target, const geometry::Aabb& box) {
    std::optional<QPointF> c[8];
    for (int i = 0; i < 8; ++i)
        c[i] = toPixel(camera, target.frame.applyPoint({(i & 1) ? box.max.x : box.min.x,
                                                       (i & 2) ? box.max.y : box.min.y,
                                                       (i & 4) ? box.max.z : box.min.z}));
    painter.setPen(QPen(kGhostColor, 1.5, Qt::DashLine));
    painter.setBrush(Qt::NoBrush);
    for (int i = 0; i < 8; ++i)
        for (int bit : {1, 2, 4})
            if (!(i & bit) && c[i] && c[i | bit]) painter.drawLine(*c[i], *c[i | bit]);
}

} // namespace

QString handleLabel(const std::string& letter, double mm, const QString& suffix) {
    const bool whole = std::abs(mm - std::round(mm)) < 1e-6;
    return QString::fromStdString(letter) + QLatin1Char(' ') + QString::number(mm, 'f', whole ? 0 : 1) + suffix;
}

void paintHandleOverlay(QPainter& painter, const Camera& camera, const ResizeTarget& target,
                        std::optional<std::size_t> hovered, const std::optional<HandleDragState>& drag,
                        const QString& lengthSuffix) {
    painter.setRenderHint(QPainter::Antialiasing);
    if (drag) drawGhost(painter, camera, target, ghostBox(target, drag->handle, drag->value));

    for (std::size_t i = 0; i < target.handles.size(); ++i) {
        if (drag && drag->handle != i) continue; // only the dragged handle while dragging
        geometry::Vec3 anchor = handleAnchor(target, i);
        if (drag) { // follow the ghost face
            const double delta = drag->value - target.handles[i].value;
            anchor = anchor + handleDirection(target, i) * delta;
        }
        const auto p = toPixel(camera, anchor);
        if (!p) continue;
        const QColor color = kAxisColors[static_cast<int>(target.handles[i].axis)];
        const bool hot = drag || hovered == i;
        // Arrow of constant screen length along the projected outward direction.
        const auto tip = toPixel(camera, anchor + handleDirection(target, i));
        if (tip) {
            QPointF dir = *tip - *p;
            const double len = std::hypot(dir.x(), dir.y());
            if (len > 1e-9) {
                dir /= len;
                const QPointF end = *p + dir * kArrowPx;
                const QPointF side(-dir.y(), dir.x());
                painter.setPen(QPen(color, hot ? 3.0 : 2.0));
                painter.drawLine(*p, end);
                const QPointF head[3] = {end + dir * 7.0, end + side * 4.5, end - side * 4.5};
                painter.setBrush(color);
                painter.drawPolygon(head, 3);
            }
        }
        const double r = hot ? kHandleRadiusPx + 2.0 : kHandleRadiusPx;
        QRadialGradient g(*p - QPointF(r / 3, r / 3), r * 1.4);
        g.setColorAt(0, color.lighter(170));
        g.setColorAt(1, color.darker(hot ? 110 : 130));
        painter.setPen(QPen(QColor(255, 255, 255, 200), 1.0));
        painter.setBrush(g);
        painter.drawEllipse(*p, r, r);

        if (drag) {
            const QString text = handleLabel(target.handles[i].label, drag->value, lengthSuffix);
            const QFontMetrics fm(painter.font());
            QRectF box = fm.boundingRect(text).toRectF().adjusted(-5, -3, 5, 3);
            box.moveCenter(*p + QPointF(0, -(r + 16)));
            painter.setPen(QPen(kGhostColor, 1.0));
            painter.setBrush(QColor(255, 255, 255, 235));
            painter.drawRoundedRect(box, 3, 3);
            painter.setPen(QColor(20, 20, 30));
            painter.drawText(box, Qt::AlignCenter, text);
        }
    }
}

} // namespace fos::viewport::detail
