// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/drawings/primitives.hpp"

#include <cmath>

namespace fos::drawings {

double defaultLineWeight(LineStyle s) noexcept {
    switch (s) {
    case LineStyle::Visible: return 0.5;
    case LineStyle::Hidden: return 0.25;
    case LineStyle::Center: return 0.18;
    case LineStyle::Dimension: return 0.18;
    case LineStyle::Thin: return 0.18;
    case LineStyle::Border: return 0.7;
    }
    return 0.25;
}

std::vector<double> dashPattern(LineStyle s) {
    switch (s) {
    case LineStyle::Hidden: return {2.0, 1.0};
    case LineStyle::Center: return {8.0, 1.5, 1.0, 1.5};
    default: return {};
    }
}

const char* toString(LineStyle s) noexcept {
    switch (s) {
    case LineStyle::Visible: return "visible";
    case LineStyle::Hidden: return "hidden";
    case LineStyle::Center: return "center";
    case LineStyle::Dimension: return "dimension";
    case LineStyle::Thin: return "thin";
    case LineStyle::Border: return "border";
    }
    return "?";
}

std::vector<Vec2> markerOutline(const Marker& m) {
    const double a = core::degToRad(m.angleDeg);
    const Vec2 d{std::cos(a), std::sin(a)};
    const Vec2 n{-d.y, d.x};
    switch (m.kind) {
    case MarkerKind::Arrow: {
        const Vec2 base = m.position - d * m.size;
        return {m.position, base + n * (m.size / 6.0), base - n * (m.size / 6.0)};
    }
    case MarkerKind::Tick: {
        const double t = core::degToRad(m.angleDeg + 45.0);
        const Vec2 h = Vec2{std::cos(t), std::sin(t)} * (m.size * 0.5);
        return {m.position - h, m.position + h};
    }
    case MarkerKind::Dot: return {};
    }
    return {};
}

std::size_t codePointCount(std::string_view utf8) noexcept {
    std::size_t n = 0;
    for (const char c : utf8)
        if ((static_cast<unsigned char>(c) & 0xC0U) != 0x80U) ++n;
    return n;
}

double estimateTextWidth(std::string_view utf8, double height) noexcept {
    return 0.6 * height * static_cast<double>(codePointCount(utf8));
}

Rect2 textBox(const Text& t) noexcept {
    const double w = estimateTextWidth(t.text, t.height);
    double x0 = 0.0;
    if (t.hAlign == HAlign::Center) x0 = -w * 0.5;
    if (t.hAlign == HAlign::Right) x0 = -w;
    double baseline = 0.0; // local y of the baseline relative to the anchor
    if (t.vAlign == VAlign::Middle) baseline = -0.35 * t.height;
    if (t.vAlign == VAlign::Top) baseline = -0.7 * t.height;
    const double y0 = baseline - 0.2 * t.height, y1 = baseline + 0.75 * t.height;
    const double a = core::degToRad(t.rotationDeg);
    const double c = std::cos(a), s = std::sin(a);
    Rect2 r;
    for (const Vec2 p : {Vec2{x0, y0}, Vec2{x0 + w, y0}, Vec2{x0 + w, y1}, Vec2{x0, y1}})
        r.expand(t.position + Vec2{p.x * c - p.y * s, p.x * s + p.y * c});
    return r;
}

namespace {
bool angleInSweep(double deg, double start, double end) noexcept {
    const auto norm = [](double v) {
        v = std::fmod(v, 360.0);
        return v < 0 ? v + 360.0 : v;
    };
    double sweep = norm(end - start);
    if (sweep == 0.0) sweep = 360.0;
    return norm(deg - start) <= sweep;
}

struct BoundsVisitor {
    Rect2 operator()(const Line& l) const noexcept { return Rect2::fromCorners(l.a, l.b); }
    Rect2 operator()(const Polyline& p) const noexcept {
        Rect2 r;
        for (const Vec2& v : p.points) r.expand(v);
        return r;
    }
    Rect2 operator()(const Circle& c) const noexcept {
        return {{c.center.x - c.radius, c.center.y - c.radius}, {c.center.x + c.radius, c.center.y + c.radius}};
    }
    Rect2 operator()(const Arc& a) const noexcept {
        Rect2 r;
        const auto at = [&](double deg) {
            const double t = core::degToRad(deg);
            return a.center + Vec2{std::cos(t), std::sin(t)} * a.radius;
        };
        r.expand(at(a.startDeg));
        r.expand(at(a.endDeg));
        for (const double q : {0.0, 90.0, 180.0, 270.0})
            if (angleInSweep(q, a.startDeg, a.endDeg)) r.expand(at(q));
        return r;
    }
    Rect2 operator()(const Text& t) const noexcept { return textBox(t); }
    Rect2 operator()(const Marker& m) const noexcept {
        Rect2 r;
        if (m.kind == MarkerKind::Dot) {
            r.expand(m.position - Vec2{m.size * 0.5, m.size * 0.5});
            r.expand(m.position + Vec2{m.size * 0.5, m.size * 0.5});
        } else {
            for (const Vec2& v : markerOutline(m)) r.expand(v);
        }
        return r;
    }
};

struct TranslateVisitor {
    Vec2 d;
    void operator()(Line& l) const noexcept {
        l.a = l.a + d;
        l.b = l.b + d;
    }
    void operator()(Polyline& p) const noexcept {
        for (Vec2& v : p.points) v = v + d;
    }
    void operator()(Circle& c) const noexcept { c.center = c.center + d; }
    void operator()(Arc& a) const noexcept { a.center = a.center + d; }
    void operator()(Text& t) const noexcept { t.position = t.position + d; }
    void operator()(Marker& m) const noexcept { m.position = m.position + d; }
};
} // namespace

Rect2 boundsOf(const Primitive& p) noexcept { return std::visit(BoundsVisitor{}, p); }

Rect2 boundsOf(const std::vector<Primitive>& ps) noexcept {
    Rect2 r;
    for (const auto& p : ps) r.expand(boundsOf(p));
    return r;
}

const std::string& sourceIdOf(const Primitive& p) noexcept {
    return std::visit([](const auto& v) -> const std::string& { return v.sourceId; }, p);
}
const std::string& elementIdOf(const Primitive& p) noexcept {
    return std::visit([](const auto& v) -> const std::string& { return v.elementId; }, p);
}
void translate(Primitive& p, Vec2 d) noexcept { std::visit(TranslateVisitor{d}, p); }

} // namespace fos::drawings
