// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/drawings/render.hpp"

#include <cmath>

namespace fos::drawings {
namespace {

Line makeLine(Vec2 a, Vec2 b, LineStyle style, const std::string& source, const std::string& element) {
    return Line{a, b, style, defaultLineWeight(style), source, element};
}

double angleOf(Vec2 d) noexcept { return core::radToDeg(std::atan2(d.y, d.x)); }

Marker makeMarker(MarkerKind kind, Vec2 pos, double angleDeg, double size, const std::string& element) {
    Marker m;
    m.kind = kind;
    m.position = pos;
    m.angleDeg = angleDeg;
    m.size = size;
    m.elementId = element;
    return m;
}

void renderEdgeMark(const EdgeBandMark& m, const ViewPlacement& pl, const DrawingStyle& st,
                    std::vector<Primitive>& out) {
    const Vec2 a = pl.toPaper(m.a), b = pl.toPaper(m.b);
    const double len = length(b - a);
    if (len <= core::kLengthEps) return;
    const Vec2 dir = (b - a) * (1.0 / len);
    const double inset = std::min(st.edgeMarkOffset, len * 0.25);
    Line l = makeLine(a + dir * inset + m.inward * st.edgeMarkOffset, b - dir * inset + m.inward * st.edgeMarkOffset,
                      LineStyle::Visible, m.sourceId, m.id);
    l.weight = st.edgeMarkWeight;
    out.emplace_back(std::move(l));
    Text t;
    t.position = (a + b) * 0.5 + m.inward * (st.edgeMarkOffset + st.edgeCodeHeight) + m.textShift;
    t.height = st.edgeCodeHeight;
    t.hAlign = HAlign::Center;
    t.vAlign = VAlign::Middle;
    t.text = m.code;
    t.sourceId = m.sourceId;
    t.elementId = m.id;
    out.emplace_back(std::move(t));
}

void renderGrainArrow(const GrainArrow& g, const ViewPlacement& pl, const DrawingStyle& st,
                      std::vector<Primitive>& out) {
    const double dl = length(g.direction);
    if (dl <= core::kLengthEps) return;
    const Vec2 dir = g.direction * (1.0 / dl);
    const Vec2 c = pl.toPaper(g.center) + g.textShift;
    const Vec2 a = c - dir * (st.grainArrowLength * 0.5), b = c + dir * (st.grainArrowLength * 0.5);
    out.emplace_back(makeLine(a, b, LineStyle::Thin, {}, g.id));
    out.emplace_back(makeMarker(MarkerKind::Arrow, a, angleOf(-dir), st.dimension.arrowSize, g.id));
    out.emplace_back(makeMarker(MarkerKind::Arrow, b, angleOf(dir), st.dimension.arrowSize, g.id));
}

void renderNote(const Note& n, const ViewPlacement& pl, std::vector<Primitive>& out) {
    Text t;
    t.position = pl.toPaper(n.anchor) + n.paperOffset + n.textShift;
    t.height = n.height;
    t.hAlign = n.hAlign;
    t.vAlign = n.vAlign;
    t.rotationDeg = n.rotationDeg;
    t.text = n.text;
    t.sourceId = n.sourceId;
    t.elementId = n.id;
    out.emplace_back(std::move(t));
}

void renderLeader(const LeaderNote& l, const ViewPlacement& pl, const DrawingStyle& st, std::vector<Primitive>& out) {
    const Vec2 tip = pl.toPaper(l.target);
    const Vec2 knee = tip + l.textOffset + l.textShift;
    const double side = l.textOffset.x >= 0 ? 1.0 : -1.0;
    const Vec2 end = knee + Vec2{side * st.leaderShoulder, 0};
    if (length(knee - tip) > core::kLengthEps) {
        out.emplace_back(makeLine(tip, knee, LineStyle::Dimension, {}, l.id));
        out.emplace_back(makeMarker(MarkerKind::Arrow, tip, angleOf(tip - knee), st.dimension.arrowSize, l.id));
    }
    out.emplace_back(makeLine(knee, end, LineStyle::Dimension, {}, l.id));
    Text t;
    t.position = end + Vec2{side * 0.8, 0};
    t.height = l.height;
    t.hAlign = side > 0 ? HAlign::Left : HAlign::Right;
    t.vAlign = VAlign::Middle;
    t.text = l.text;
    t.elementId = l.id;
    out.emplace_back(std::move(t));
}

void renderDetailMarker(const DetailMarker& m, const ViewPlacement& pl, const DrawingStyle& st,
                        std::vector<Primitive>& out) {
    const Vec2 c = pl.toPaper(m.center);
    const double r = m.radius * pl.scale;
    if (r > core::kLengthEps) {
        Circle circle;
        circle.center = c;
        circle.radius = r;
        circle.style = LineStyle::Thin;
        circle.weight = defaultLineWeight(LineStyle::Thin);
        circle.elementId = m.id;
        out.emplace_back(std::move(circle));
    }
    Text t;
    const double k = std::sqrt(0.5);
    t.position = c + Vec2{k, k} * (r + 1.0) + m.textShift;
    t.height = st.detailLabelHeight;
    t.text = m.label;
    t.elementId = m.id;
    out.emplace_back(std::move(t));
}

} // namespace

DrawingStyle scaled(const DrawingStyle& s, double f) noexcept {
    DrawingStyle o = s;
    o.dimension.textHeight *= f;
    o.dimension.textGap *= f;
    o.dimension.arrowSize *= f;
    o.dimension.extensionGap *= f;
    o.dimension.extensionOvershoot *= f;
    o.dimension.firstGap *= f;
    o.dimension.stackGap *= f;
    o.hatchSpacing *= f;
    o.edgeMarkOffset *= f;
    o.edgeMarkWeight *= f;
    o.edgeCodeHeight *= f;
    o.grainArrowLength *= f;
    o.leaderShoulder *= f;
    o.detailLabelHeight *= f;
    return o;
}

void renderDimension(const Dimension& d, const ViewPlacement& placement, const DimensionStyle& style,
                     std::vector<Primitive>& out) {
    const DimensionGeometry g = layoutDimension(d, placement, style);
    if (g.ext1) out.emplace_back(makeLine(g.ext1->a, g.ext1->b, LineStyle::Dimension, {}, d.id));
    if (g.ext2) out.emplace_back(makeLine(g.ext2->a, g.ext2->b, LineStyle::Dimension, {}, d.id));
    Vec2 a = g.line.a, b = g.line.b;
    const double len = length(b - a);
    if (len > core::kLengthEps) {
        const Vec2 dir = (b - a) * (1.0 / len);
        if (style.terminator == DimensionTerminator::Tick) {
            const double ang = angleOf(dir);
            out.emplace_back(makeLine(a, b, LineStyle::Dimension, {}, d.id));
            out.emplace_back(makeMarker(MarkerKind::Tick, a, ang, style.arrowSize, d.id));
            out.emplace_back(makeMarker(MarkerKind::Tick, b, ang, style.arrowSize, d.id));
        } else if (g.terminatorsOutside) {
            // Short span: arrows sit outside and point inwards at the extension lines.
            const double tail = style.arrowSize * 1.6;
            out.emplace_back(makeLine(a - dir * tail, b + dir * tail, LineStyle::Dimension, {}, d.id));
            out.emplace_back(makeMarker(MarkerKind::Arrow, a, angleOf(dir), style.arrowSize, d.id));
            out.emplace_back(makeMarker(MarkerKind::Arrow, b, angleOf(-dir), style.arrowSize, d.id));
        } else {
            out.emplace_back(makeLine(a, b, LineStyle::Dimension, {}, d.id));
            out.emplace_back(makeMarker(MarkerKind::Arrow, a, angleOf(-dir), style.arrowSize, d.id));
            out.emplace_back(makeMarker(MarkerKind::Arrow, b, angleOf(dir), style.arrowSize, d.id));
        }
    }
    out.emplace_back(g.text);
}

std::vector<Primitive> renderView(const View& view, const ViewAnnotations& ann, const ViewPlacement& pl,
                                  const DrawingStyle& st) {
    std::vector<Primitive> out;
    if (!(pl.scale > 0)) return out;
    for (const ViewSegment& s : view.segments)
        out.emplace_back(makeLine(pl.toPaper(s.a), pl.toPaper(s.b), s.style, s.sourceId, {}));
    for (const ViewCircle& c : view.circles) {
        Circle circle;
        circle.center = pl.toPaper(c.center);
        circle.radius = c.radius * pl.scale;
        circle.style = c.style;
        circle.weight = defaultLineWeight(c.style);
        circle.sourceId = c.sourceId;
        out.emplace_back(std::move(circle));
    }
    for (const CutRegion& cut : view.cuts) {
        // Hatch in view space with the spacing converted to model mm, so that the
        // pattern stays anchored to the model when the view is moved on the sheet.
        for (const Segment2& h : hatchRect(cut.rect, cut.hatchAngleDeg, st.hatchSpacing / pl.scale))
            out.emplace_back(makeLine(pl.toPaper(h.a), pl.toPaper(h.b), LineStyle::Thin, cut.sourceId, {}));
    }
    for (const EdgeBandMark& m : ann.edgeBandMarks)
        if (!m.hidden) renderEdgeMark(m, pl, st, out);
    for (const GrainArrow& g : ann.grainArrows)
        if (!g.hidden) renderGrainArrow(g, pl, st, out);
    for (const Dimension& d : ann.dimensions)
        if (!d.hidden) renderDimension(d, pl, st.dimension, out);
    for (const Note& n : ann.notes)
        if (!n.hidden) renderNote(n, pl, out);
    for (const LeaderNote& l : ann.leaders)
        if (!l.hidden) renderLeader(l, pl, st, out);
    for (const DetailMarker& m : ann.detailMarkers)
        if (!m.hidden) renderDetailMarker(m, pl, st, out);
    return out;
}

} // namespace fos::drawings
