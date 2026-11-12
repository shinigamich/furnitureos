// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/printing/paint.hpp"

#include "paint_detail.hpp"

#include <QFont>
#include <QFontMetricsF>
#include <QPaintDevice>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>

#include <cmath>
#include <variant>

namespace fos::printing {

namespace detail {

namespace {
/// Fonts are created at this pixel size and scaled by the painter, which gives exact
/// (unhinted, non-integer) text sizes on every device.
constexpr int kFontUnits = 100;
} // namespace

void drawTextMm(QPainter& painter, QPointF anchor, double height, int hAlign, double rotationCcwDeg,
                const QString& text, const QString& family, const QColor& color) {
    if (text.isEmpty() || !(height > 0) || !std::isfinite(height) || !std::isfinite(anchor.x()) ||
        !std::isfinite(anchor.y()) || !std::isfinite(rotationCcwDeg))
        return;
    QFont font(family);
    font.setStyleHint(QFont::SansSerif, QFont::PreferOutline);
    font.setHintingPreference(QFont::PreferNoHinting);
    font.setKerning(true);
    font.setPixelSize(kFontUnits);
    const double advance = QFontMetricsF(font).horizontalAdvance(text);
    const double x = hAlign < 0 ? 0.0 : hAlign == 0 ? -advance / 2 : -advance;

    painter.save();
    painter.translate(anchor);
    painter.rotate(-rotationCcwDeg); // QPainter rotates clockwise in a Y-down system
    painter.scale(height / kFontUnits, height / kFontUnits);
    painter.setFont(font);
    painter.setPen(color);
    painter.setBrush(Qt::NoBrush);
    painter.drawText(QPointF(x, 0), text);
    painter.restore();
}

void scaleToMm(QPainter& painter, double unitsPerMm) {
    if (unitsPerMm > 0 && std::isfinite(unitsPerMm)) {
        painter.scale(unitsPerMm, unitsPerMm);
    } else if (const QPaintDevice* device = painter.device()) {
        painter.scale(device->logicalDpiX() / 25.4, device->logicalDpiY() / 25.4);
    }
}

} // namespace detail

namespace {

using namespace fos::drawings;

bool finite(Vec2 p) { return std::isfinite(p.x) && std::isfinite(p.y); }

struct Painter {
    QPainter& qp;
    const Sheet& sheet;
    const PaintOptions& opt;

    QPointF pt(Vec2 p) const { return {p.x, sheet.height - p.y}; }

    QPen pen(LineStyle style, double weight) const {
        // A weight that is not positive is drawn as a 0.01 mm hairline rather than
        // with Qt's device-dependent cosmetic pen.
        const double w = (std::isfinite(weight) && weight > 0) ? weight : 0.01;
        QPen p(opt.color);
        p.setWidthF(w);
        p.setCapStyle(Qt::FlatCap);
        p.setJoinStyle(Qt::MiterJoin);
        const std::vector<double> dash = dashPattern(style);
        if (!dash.empty()) {
            QList<qreal> pattern; // QPen measures dashes in multiples of the pen width
            for (const double d : dash) pattern.push_back(d / w);
            if (pattern.size() % 2 != 0) pattern.append(pattern); // odd lists repeat, as in SVG
            p.setDashPattern(pattern);
        }
        return p;
    }
    void stroke(LineStyle style, double weight) {
        qp.setPen(pen(style, weight));
        qp.setBrush(Qt::NoBrush);
    }
    void fill() {
        qp.setPen(Qt::NoPen);
        qp.setBrush(opt.color);
    }

    void operator()(const Line& l) {
        if (!finite(l.a) || !finite(l.b)) return;
        stroke(l.style, l.weight);
        qp.drawLine(QLineF(pt(l.a), pt(l.b)));
    }
    void operator()(const Polyline& p) {
        if (p.points.size() < 2) return;
        QPolygonF poly;
        for (const Vec2 v : p.points) {
            if (!finite(v)) return;
            poly << pt(v);
        }
        stroke(p.style, p.weight);
        if (p.closed) qp.drawPolygon(poly);
        else qp.drawPolyline(poly);
    }
    void operator()(const Circle& c) {
        if (!finite(c.center) || !std::isfinite(c.radius) || c.radius <= 0) return;
        stroke(c.style, c.weight);
        qp.drawEllipse(pt(c.center), c.radius, c.radius);
    }
    void operator()(const Arc& a) {
        if (!finite(a.center) || !std::isfinite(a.radius) || a.radius <= 0 || !std::isfinite(a.startDeg) ||
            !std::isfinite(a.endDeg))
            return;
        double sweep = std::fmod(a.endDeg - a.startDeg, 360.0);
        if (sweep <= 0) sweep += 360.0;
        const QPointF c = pt(a.center);
        const QRectF box(c.x() - a.radius, c.y() - a.radius, 2 * a.radius, 2 * a.radius);
        // QPainterPath angles are counter-clockwise as seen on the (Y-down) device,
        // which is counter-clockwise on paper as well.
        QPainterPath path;
        path.arcMoveTo(box, a.startDeg);
        path.arcTo(box, a.startDeg, sweep);
        stroke(a.style, a.weight);
        qp.drawPath(path);
    }
    void operator()(const Text& t) {
        if (!finite(t.position)) return;
        double down = 0.0;
        if (t.vAlign == VAlign::Middle) down = 0.35 * t.height;
        if (t.vAlign == VAlign::Top) down = 0.7 * t.height;
        const double a = core::degToRad(t.rotationDeg);
        const Vec2 p = t.position + Vec2{std::sin(a), -std::cos(a)} * down;
        const int h = t.hAlign == HAlign::Left ? -1 : t.hAlign == HAlign::Center ? 0 : 1;
        detail::drawTextMm(qp, pt(p), t.height, h, t.rotationDeg, QString::fromUtf8(t.text.data(),
                           static_cast<qsizetype>(t.text.size())), opt.fontFamily, opt.color);
    }
    void operator()(const Marker& m) {
        if (!finite(m.position) || !std::isfinite(m.size) || !std::isfinite(m.angleDeg)) return;
        const std::vector<Vec2> pts = markerOutline(m);
        switch (m.kind) {
        case MarkerKind::Arrow:
            if (pts.size() < 3) return;
            fill();
            qp.drawPolygon(QPolygonF({pt(pts[0]), pt(pts[1]), pt(pts[2])}));
            break;
        case MarkerKind::Tick:
            if (pts.size() < 2) return;
            stroke(LineStyle::Thin, 0.35); // same fixed weight as the SVG exporter
            qp.drawLine(QLineF(pt(pts[0]), pt(pts[1])));
            break;
        case MarkerKind::Dot:
            if (m.size <= 0) return;
            fill();
            qp.drawEllipse(pt(m.position), m.size * 0.5, m.size * 0.5);
            break;
        }
    }
};

} // namespace

void paintSheet(QPainter& painter, const drawings::Sheet& sheet, const PaintOptions& options) {
    if (!(sheet.width > 0) || !(sheet.height > 0) || !std::isfinite(sheet.width) || !std::isfinite(sheet.height))
        return;
    painter.save();
    detail::scaleToMm(painter, options.unitsPerMm);
    painter.setRenderHint(QPainter::Antialiasing, options.antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing, options.antialiasing);
    if (options.background.isValid())
        painter.fillRect(QRectF(0, 0, sheet.width, sheet.height), options.background);
    Painter p{painter, sheet, options};
    for (const drawings::Primitive& prim : sheet.primitives) std::visit(p, prim);
    painter.restore();
}

QImage renderPreview(const drawings::Sheet& sheet, double pixelsPerMm, const PaintOptions& options) {
    if (!(pixelsPerMm > 0) || !std::isfinite(pixelsPerMm) || !(sheet.width > 0) || !(sheet.height > 0) ||
        !std::isfinite(sheet.width) || !std::isfinite(sheet.height))
        return {};
    const double w = std::ceil(sheet.width * pixelsPerMm - 1e-9), h = std::ceil(sheet.height * pixelsPerMm - 1e-9);
    if (w < 1 || h < 1 || w > kMaxPreviewSidePx || h > kMaxPreviewSidePx) return {};
    QImage image(static_cast<int>(w), static_cast<int>(h), QImage::Format_ARGB32_Premultiplied);
    if (image.isNull()) return {};
    image.fill(options.background.isValid() ? options.background : QColor(Qt::white));
    QPainter painter(&image);
    PaintOptions o = options;
    o.unitsPerMm = pixelsPerMm;
    o.background = QColor(); // already filled (also covers the fractional last pixel)
    paintSheet(painter, sheet, o);
    painter.end();
    return image;
}

} // namespace fos::printing
