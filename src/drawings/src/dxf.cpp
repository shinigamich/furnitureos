// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/drawings/dxf.hpp"

#include "format.hpp"

#include <cmath>
#include <cstdio>

namespace fos::drawings {

using detail::num;

std::string dxfEscapeText(std::string_view utf8) {
    std::string out;
    for (const std::uint32_t cp : detail::decodeUtf8(utf8)) {
        if (cp >= 0x20 && cp < 0x7F) {
            out += static_cast<char>(cp);
        } else if (cp >= 0x80 && cp <= 0xFFFF) {
            char buf[16];
            std::snprintf(buf, sizeof buf, "\\U+%04X", static_cast<unsigned>(cp));
            out += buf;
        } else {
            out += '?';
        }
    }
    return out;
}

namespace {

const char* layerOf(LineStyle s) noexcept {
    switch (s) {
    case LineStyle::Visible:
    case LineStyle::Border: return "VISIBLE";
    case LineStyle::Hidden: return "HIDDEN";
    case LineStyle::Center: return "CENTER";
    case LineStyle::Dimension: return "DIMENSION";
    case LineStyle::Thin: return "HATCH";
    }
    return "0";
}

struct Writer {
    std::string out;
    DxfEntityCounts counts;

    void pair(int code, std::string_view value) {
        out += std::to_string(code);
        out += '\n';
        out += value;
        out += '\n';
    }
    void real(int code, double v) { pair(code, num(v, 6)); }
    void integer(int code, long long v) { pair(code, std::to_string(v)); }

    void line(Vec2 a, Vec2 b, const char* layer) {
        pair(0, "LINE");
        pair(8, layer);
        real(10, a.x); real(20, a.y); real(30, 0);
        real(11, b.x); real(21, b.y); real(31, 0);
        ++counts.lines;
    }
    void circle(Vec2 c, double r, const char* layer) {
        pair(0, "CIRCLE");
        pair(8, layer);
        real(10, c.x); real(20, c.y); real(30, 0);
        real(40, r);
        ++counts.circles;
    }

    void operator()(const Line& l) { line(l.a, l.b, layerOf(l.style)); }
    void operator()(const Polyline& p) {
        if (p.points.size() < 2) return;
        for (std::size_t i = 0; i + 1 < p.points.size(); ++i) line(p.points[i], p.points[i + 1], layerOf(p.style));
        if (p.closed && p.points.size() > 2) line(p.points.back(), p.points.front(), layerOf(p.style));
    }
    void operator()(const Circle& c) { circle(c.center, c.radius, layerOf(c.style)); }
    void operator()(const Arc& a) {
        pair(0, "ARC");
        pair(8, layerOf(a.style));
        real(10, a.center.x); real(20, a.center.y); real(30, 0);
        real(40, a.radius);
        real(50, a.startDeg); // DXF arcs are counter-clockwise from 50 to 51, like ours
        real(51, a.endDeg);
        ++counts.arcs;
    }
    void operator()(const Text& t) {
        pair(0, "TEXT");
        pair(8, "TEXT");
        real(10, t.position.x); real(20, t.position.y); real(30, 0);
        real(40, t.height);
        pair(1, dxfEscapeText(t.text));
        if (std::fabs(t.rotationDeg) > 1e-9) real(50, t.rotationDeg);
        pair(7, "STANDARD");
        const int h = t.hAlign == HAlign::Left ? 0 : t.hAlign == HAlign::Center ? 1 : 2;
        const int v = t.vAlign == VAlign::Baseline ? 0 : t.vAlign == VAlign::Middle ? 2 : 3;
        if (h != 0 || v != 0) {
            integer(72, h);
            integer(73, v);
            real(11, t.position.x); real(21, t.position.y); real(31, 0); // alignment point
        }
        ++counts.texts;
    }
    void operator()(const Marker& m) {
        const std::vector<Vec2> pts = markerOutline(m);
        switch (m.kind) {
        case MarkerKind::Arrow:
            for (std::size_t i = 0; i < 3; ++i) line(pts[i], pts[(i + 1) % 3], "DIMENSION");
            break;
        case MarkerKind::Tick: line(pts[0], pts[1], "DIMENSION"); break;
        case MarkerKind::Dot: circle(m.position, m.size * 0.5, "DIMENSION"); break;
        }
    }
};

void ltype(Writer& w, const char* name, const char* description, std::initializer_list<double> pattern) {
    w.pair(0, "LTYPE");
    w.pair(2, name);
    w.integer(70, 0);
    w.pair(3, description);
    w.integer(72, 65);
    w.integer(73, static_cast<long long>(pattern.size()));
    double total = 0;
    for (const double p : pattern) total += std::fabs(p);
    w.real(40, total);
    for (const double p : pattern) w.real(49, p);
}

void layer(Writer& w, const char* name, int color, const char* linetype) {
    w.pair(0, "LAYER");
    w.pair(2, name);
    w.integer(70, 0);
    w.integer(62, color);
    w.pair(6, linetype);
}

} // namespace

DxfEntityCounts countDxfEntities(const std::vector<Primitive>& primitives) {
    Writer w;
    for (const Primitive& p : primitives) std::visit(w, p);
    return w.counts;
}

std::string toDxf(const std::vector<Primitive>& primitives, const DxfOptions& options) {
    Rect2 ext = boundsOf(primitives);
    if (ext.empty()) ext = {{0, 0}, {0, 0}};
    Writer w;
    w.pair(0, "SECTION");
    w.pair(2, "HEADER");
    w.pair(9, "$ACADVER");
    w.pair(1, "AC1009");
    w.pair(9, "$INSBASE");
    w.real(10, 0); w.real(20, 0); w.real(30, 0);
    w.pair(9, "$EXTMIN");
    w.real(10, ext.min.x); w.real(20, ext.min.y); w.real(30, 0);
    w.pair(9, "$EXTMAX");
    w.real(10, ext.max.x); w.real(20, ext.max.y); w.real(30, 0);
    w.pair(9, "$LUNITS"); // 2 = decimal
    w.integer(70, 2);
    w.pair(9, "$LTSCALE");
    w.real(40, options.linetypeScale > 0 ? options.linetypeScale : 1.0);
    if (options.writeUnitVariables) {
        w.pair(9, "$MEASUREMENT"); // 1 = metric
        w.integer(70, 1);
        w.pair(9, "$INSUNITS"); // 4 = millimetres
        w.integer(70, 4);
    }
    w.pair(0, "ENDSEC");

    w.pair(0, "SECTION");
    w.pair(2, "TABLES");
    w.pair(0, "TABLE");
    w.pair(2, "LTYPE");
    w.integer(70, 3);
    ltype(w, "CONTINUOUS", "Solid line", {});
    ltype(w, "DASHED", "Dashed __ __ __", {2.0, -1.0});
    ltype(w, "CENTER", "Center ____ _ ____", {8.0, -1.5, 1.0, -1.5});
    w.pair(0, "ENDTAB");
    w.pair(0, "TABLE");
    w.pair(2, "LAYER");
    w.integer(70, 7);
    layer(w, "0", 7, "CONTINUOUS");
    layer(w, "VISIBLE", 7, "CONTINUOUS");
    layer(w, "HIDDEN", 8, "DASHED");
    layer(w, "CENTER", 1, "CENTER");
    layer(w, "DIMENSION", 3, "CONTINUOUS");
    layer(w, "HATCH", 8, "CONTINUOUS");
    layer(w, "TEXT", 7, "CONTINUOUS");
    w.pair(0, "ENDTAB");
    w.pair(0, "TABLE");
    w.pair(2, "STYLE");
    w.integer(70, 1);
    w.pair(0, "STYLE");
    w.pair(2, "STANDARD");
    w.integer(70, 0);
    w.real(40, 0);
    w.real(41, 1);
    w.real(50, 0);
    w.integer(71, 0);
    w.real(42, 2.5);
    w.pair(3, "txt");
    w.pair(4, "");
    w.pair(0, "ENDTAB");
    w.pair(0, "ENDSEC");

    w.pair(0, "SECTION");
    w.pair(2, "BLOCKS");
    w.pair(0, "ENDSEC");

    w.pair(0, "SECTION");
    w.pair(2, "ENTITIES");
    for (const Primitive& p : primitives) std::visit(w, p);
    w.pair(0, "ENDSEC");
    w.pair(0, "EOF");
    return w.out;
}

std::vector<Primitive> modelSpacePrimitives(const View& view, const ViewAnnotations& annotations,
                                            const DrawingStyle& style, double annotationScale) {
    const double f = annotationScale > 0 ? annotationScale : 1.0;
    // Paper-sized annotation fields stored in the entities (offsets, shifts, heights)
    // are scaled together with the style.
    ViewAnnotations a = annotations;
    for (Dimension& d : a.dimensions) {
        d.offset *= f;
        d.textShift = d.textShift * f;
    }
    for (Note& n : a.notes) {
        n.paperOffset = n.paperOffset * f;
        n.textShift = n.textShift * f;
        n.height *= f;
    }
    for (LeaderNote& l : a.leaders) {
        l.textOffset = l.textOffset * f;
        l.textShift = l.textShift * f;
        l.height *= f;
    }
    for (DetailMarker& m : a.detailMarkers) m.textShift = m.textShift * f;
    for (EdgeBandMark& m : a.edgeBandMarks) m.textShift = m.textShift * f;
    for (GrainArrow& g : a.grainArrows) g.textShift = g.textShift * f;
    return renderView(view, a, ViewPlacement{{0, 0}, 1.0}, scaled(style, f));
}

std::string viewToDxf(const View& view, const ViewAnnotations& annotations, const DrawingStyle& style,
                      double annotationScale, DxfOptions options) {
    options.linetypeScale = annotationScale > 0 ? annotationScale : 1.0;
    return toDxf(modelSpacePrimitives(view, annotations, style, annotationScale), options);
}

} // namespace fos::drawings
