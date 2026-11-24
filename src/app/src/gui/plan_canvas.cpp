// SPDX-License-Identifier: AGPL-3.0-only
#include "plan_canvas.hpp"

#include <QFileInfo>
#include <QMouseEvent>
#include <QPainter>
#include <QPolygonF>
#include <QSvgGenerator>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace fos::app::gui {
namespace {

QPainterPath unionPath(const PlanFootprint& f) {
    QPainterPath path;
    path.setFillRule(Qt::WindingFill);
    for (const auto& poly : f.outlines) {
        const geometry::Polygon2 ccw = geometry::normalizedCcw(poly);
        QPolygonF q;
        for (const auto& v : ccw.vertices) q << QPointF(v.x, v.y);
        if (!q.isEmpty()) q << q.front();
        path.addPolygon(q);
    }
    return path.simplified();
}

QRectF toQt(const geometry::Rect2& r) { return QRectF(QPointF(r.min.x, r.min.y), QPointF(r.max.x, r.max.y)); }

/// Horizontal or vertical dimension line between device points a and b with end ticks and a label.
void dimension(QPainter& p, QPointF a, QPointF b, const QString& text, double tick) {
    p.drawLine(a, b);
    const bool horizontal = std::fabs(a.y() - b.y()) < std::fabs(a.x() - b.x());
    const QPointF t = horizontal ? QPointF(0, tick) : QPointF(tick, 0);
    p.drawLine(a - t, a + t);
    p.drawLine(b - t, b + t);
    const QPointF mid = (a + b) / 2;
    const QFontMetricsF fm(p.font());
    const double w = fm.horizontalAdvance(text);
    if (horizontal) {
        p.drawText(QPointF(mid.x() - w / 2, mid.y() - fm.descent() - 2), text);
    } else {
        p.save();
        p.translate(mid.x() - 2 - fm.descent(), mid.y() + w / 2);
        p.rotate(-90);
        p.drawText(QPointF(0, 0), text);
        p.restore();
    }
}

} // namespace

PlanCanvas::PlanCanvas(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("planCanvas"));
    setMouseTracking(false);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(320, 240);
    setAutoFillBackground(false);
}

void PlanCanvas::setFootprints(std::vector<PlanFootprint> footprints) {
    footprints_ = std::move(footprints);
    paths_.clear();
    paths_.reserve(footprints_.size());
    for (const auto& f : footprints_) paths_.push_back(unionPath(f));
    if (!fitted_ && !footprints_.empty()) fitToView();
    update();
}

void PlanCanvas::setRoom(std::optional<project::RoomSettings> room) {
    room_ = std::move(room);
    update();
}

void PlanCanvas::setSelectedItem(const std::string& itemId) {
    if (selected_ == itemId) return;
    selected_ = itemId;
    update();
}

geometry::Rect2 PlanCanvas::contentRect() const {
    geometry::Rect2 r = planExtents(footprints_);
    if (room_ && project::checkRoomSettings(*room_).ok()) r.expand(planRoomInner(*room_).inflated(room_->wallThickness));
    return r;
}

void PlanCanvas::fitToView() {
    const geometry::Rect2 r = contentRect();
    if (r.empty() || width() <= 0 || height() <= 0) return;
    constexpr double margin = 60; // px: room for the overall dimension lines
    const double w = std::max(r.width(), 1.0), h = std::max(r.height(), 1.0);
    scale_ = std::clamp(std::min((width() - 2 * margin) / w, (height() - 2 * margin) / h), 0.005, 50.0);
    const geometry::Vec2 c = r.center();
    origin_ = QPointF(width() / 2.0 - c.x * scale_, height() / 2.0 + c.y * scale_);
    fitted_ = true;
    update();
}

void PlanCanvas::zoomBy(double factor) {
    const QPointF centre(width() / 2.0, height() / 2.0);
    const double next = std::clamp(scale_ * factor, 0.005, 50.0);
    origin_ = centre + (origin_ - centre) * (next / scale_);
    scale_ = next;
    update();
}

geometry::Vec2 PlanCanvas::toProject(QPointF s) const {
    return {(s.x() - origin_.x()) / scale_, (origin_.y() - s.y()) / scale_};
}

void PlanCanvas::render(QPainter& p, double scale, QPointF origin, double textPixels, bool interactive) const {
    const QTransform world(scale, 0, 0, -scale, origin.x(), origin.y());
    auto map = [&](double x, double y) { return world.map(QPointF(x, y)); };
    p.setRenderHint(QPainter::Antialiasing, true);
    QFont font = p.font();
    font.setPixelSize(std::max(1, static_cast<int>(std::lround(textPixels))));
    p.setFont(font);
    const QFontMetricsF fm(font);

    // Footprints as they are drawn (the dragged one moved by the snapped delta).
    std::vector<PlanFootprint> shown = footprints_;
    const geometry::Vec2 moveBy = (interactive && drag_ == Drag::Move && snap_) ? snap_->delta : geometry::Vec2{};
    for (auto& f : shown)
        if (f.itemId == dragItem_ && (moveBy.x != 0 || moveBy.y != 0)) {
            f.bounds = {f.bounds.min + moveBy, f.bounds.max + moveBy};
            for (auto& poly : f.outlines) poly = geometry::translated(poly, moveBy);
        }

    // Room: walls outside the inner rectangle.
    const bool hasRoom = room_ && project::checkRoomSettings(*room_).ok();
    if (hasRoom) {
        const QRectF inner = world.mapRect(toQt(planRoomInner(*room_)));
        const QRectF outer = world.mapRect(toQt(planRoomInner(*room_).inflated(room_->wallThickness)));
        QPainterPath walls;
        walls.addRect(outer);
        walls.addRect(inner);
        walls.setFillRule(Qt::OddEvenFill);
        p.setPen(QPen(QColor(60, 60, 60), 1.2));
        p.setBrush(QBrush(QColor(150, 150, 150), Qt::BDiagPattern));
        p.drawPath(walls);
        p.setBrush(Qt::NoBrush);
        p.drawRect(inner);
    }

    // Items.
    for (std::size_t i = 0; i < shown.size(); ++i) {
        const bool selected = shown[i].itemId == selected_;
        QPainterPath path = paths_[i];
        if (shown[i].itemId == dragItem_) path.translate(moveBy.x, moveBy.y);
        p.setPen(QPen(selected ? QColor(20, 90, 200) : QColor(40, 40, 40), selected ? 2.2 : 1.2));
        p.setBrush(selected ? QColor(200, 220, 250) : QColor(235, 225, 205));
        p.drawPath(world.map(path));
    }
    p.setBrush(Qt::NoBrush);

    // Snap guides.
    if (interactive && drag_ == Drag::Move && snap_) {
        p.setPen(QPen(QColor(230, 60, 60), 1, Qt::DashLine));
        if (snap_->guideX) {
            const double x = map(*snap_->guideX, 0).x();
            p.drawLine(QPointF(x, 0), QPointF(x, height()));
        }
        if (snap_->guideY) {
            const double y = map(0, *snap_->guideY).y();
            p.drawLine(QPointF(0, y), QPointF(width(), y));
        }
    }

    // Names and sizes at the centre of each footprint.
    p.setPen(QColor(20, 20, 20));
    for (const auto& f : shown) {
        if (f.bounds.empty()) continue;
        const QPointF c = map(f.bounds.center().x, f.bounds.center().y);
        const QString name = QString::fromStdString(f.name);
        const QString size = planSizeText(f.bounds.width(), f.bounds.height());
        p.drawText(QPointF(c.x() - fm.horizontalAdvance(name) / 2, c.y() - 2), name);
        p.drawText(QPointF(c.x() - fm.horizontalAdvance(size) / 2, c.y() + fm.ascent()), size);
    }

    // Gaps along X.
    const double tick = textPixels * 0.4;
    p.setPen(QPen(QColor(0, 120, 60), 1));
    for (const auto& g : planGapsX(shown, room_))
        dimension(p, map(g.fromX, g.y), map(g.toX, g.y), planLengthText(g.gap()), tick);

    // Overall dimensions of all items: width below, depth on the left.
    const geometry::Rect2 ext = planExtents(shown);
    if (!ext.empty()) {
        p.setPen(QPen(QColor(60, 60, 140), 1));
        const double off = textPixels * 2.2;
        const QPointF bl = map(ext.min.x, ext.min.y), br = map(ext.max.x, ext.min.y), tl = map(ext.min.x, ext.max.y);
        dimension(p, bl + QPointF(0, off), br + QPointF(0, off), planLengthText(ext.width()), tick);
        dimension(p, bl - QPointF(off, 0), tl - QPointF(off, 0), planLengthText(ext.height()), tick);
    }
}

void PlanCanvas::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), Qt::white);
    render(p, scale_, origin_, std::max(9.0, font().pointSizeF() * 1.3), true);
    if (footprints_.empty()) {
        p.setPen(QColor(120, 120, 120));
        p.drawText(rect(), Qt::AlignCenter, tr("No generated items to show."));
    }
}

void PlanCanvas::mousePressEvent(QMouseEvent* event) {
    pressPos_ = lastPos_ = event->position();
    if (event->button() == Qt::MiddleButton || event->button() == Qt::RightButton) {
        drag_ = Drag::Pan;
        setCursor(Qt::ClosedHandCursor);
        return;
    }
    if (event->button() != Qt::LeftButton) return;
    const std::string id = planItemAt(footprints_, toProject(event->position()), 3.0 / scale_);
    emit itemClicked(QString::fromStdString(id));
    if (!id.empty()) {
        drag_ = Drag::Move;
        dragItem_ = id;
        snap_.reset();
    }
}

void PlanCanvas::mouseMoveEvent(QMouseEvent* event) {
    const QPointF pos = event->position();
    if (drag_ == Drag::Pan) {
        origin_ += pos - lastPos_;
        lastPos_ = pos;
        update();
    } else if (drag_ == Drag::Move) {
        if ((pos - pressPos_).manhattanLength() < 3 && !snap_) return; // a click, not a drag yet
        const QPointF d = (pos - pressPos_) / scale_;
        snap_ = snapPlanMove(footprints_, dragItem_, {d.x(), -d.y()}, kSnapPixels / scale_, room_);
        update();
    }
}

void PlanCanvas::mouseReleaseEvent(QMouseEvent*) {
    if (drag_ == Drag::Move && snap_ && (snap_->delta.x != 0 || snap_->delta.y != 0))
        emit moveRequested(QString::fromStdString(dragItem_), snap_->delta.x, snap_->delta.y);
    drag_ = Drag::None;
    dragItem_.clear();
    snap_.reset();
    unsetCursor();
    update();
}

void PlanCanvas::wheelEvent(QWheelEvent* event) {
    const double steps = event->angleDelta().y() / 120.0;
    if (steps == 0) return;
    const QPointF at = event->position();
    const double next = std::clamp(scale_ * std::pow(1.2, steps), 0.005, 50.0);
    origin_ = at + (origin_ - at) * (next / scale_);
    scale_ = next;
    update();
}

void PlanCanvas::resizeEvent(QResizeEvent*) {
    if (!fitted_) fitToView();
}

bool PlanCanvas::exportSvg(const QString& path, const QString& title) const {
    geometry::Rect2 r = contentRect();
    if (r.empty()) return false;
    const double textMm = std::clamp(std::max(r.width(), r.height()) / 80.0, 8.0, 60.0);
    r = r.inflated(textMm * 4); // overall dimension lines and labels
    QSvgGenerator svg;
    svg.setFileName(path);
    svg.setTitle(title);
    svg.setDescription(tr("FurnitureOS plan, 1 unit = 1 mm"));
    const QSize size(static_cast<int>(std::ceil(r.width())), static_cast<int>(std::ceil(r.height())));
    svg.setSize(size);
    svg.setViewBox(QRect(QPoint(0, 0), size));
    svg.setResolution(25); // ~1 unit per mm when printed
    QPainter p;
    if (!p.begin(&svg)) return false;
    p.fillRect(QRectF(QPointF(0, 0), QSizeF(size)), Qt::white);
    render(p, 1.0, QPointF(-r.min.x, r.max.y), textMm, false);
    const bool ok = p.end();
    return ok && QFileInfo(path).size() > 0;
}

} // namespace fos::app::gui
