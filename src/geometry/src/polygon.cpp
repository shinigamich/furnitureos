// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/geometry/polygon.hpp"

#include <algorithm>
#include <cmath>

namespace fos::geometry {

Polygon2 makeRectPolygon(const Rect2& r) {
    if (r.empty()) return {};
    return {{{r.min.x, r.min.y}, {r.max.x, r.min.y}, {r.max.x, r.max.y}, {r.min.x, r.max.y}}};
}

double signedArea(const Polygon2& p) noexcept {
    const std::size_t n = p.size();
    if (n < 3) return 0.0;
    // Shoelace relative to vertex 0 to limit cancellation far from the origin.
    const Vec2 o = p[0];
    double twice = 0.0;
    for (std::size_t i = 1; i + 1 < n; ++i) twice += cross(p[i] - o, p[i + 1] - o);
    return twice * 0.5;
}

double area(const Polygon2& p) noexcept { return std::fabs(signedArea(p)); }

double area(const PolygonWithHoles& p) noexcept {
    double a = area(p.outer);
    for (const Polygon2& h : p.holes) a -= area(h);
    return a;
}

double perimeter(const Polygon2& p) noexcept {
    const std::size_t n = p.size();
    if (n < 2) return 0.0;
    double sum = 0.0;
    for (std::size_t i = 0; i < n; ++i) sum += length(p[(i + 1) % n] - p[i]);
    return sum;
}

Winding winding(const Polygon2& p, double eps) noexcept {
    if (p.size() < 3) return Winding::Degenerate;
    const double a = signedArea(p);
    if (std::fabs(a) <= eps * perimeter(p) * 0.5 || a == 0.0) return Winding::Degenerate;
    return a > 0.0 ? Winding::CounterClockwise : Winding::Clockwise;
}

Polygon2 reversed(const Polygon2& p) {
    Polygon2 out = p;
    if (out.size() > 1) std::reverse(out.vertices.begin() + 1, out.vertices.end());
    return out;
}

Polygon2 normalizedCcw(const Polygon2& p) { return signedArea(p) < 0.0 ? reversed(p) : p; }

PolygonWithHoles normalized(const PolygonWithHoles& p) {
    PolygonWithHoles out;
    out.outer = normalizedCcw(p.outer);
    out.holes.reserve(p.holes.size());
    for (const Polygon2& h : p.holes) out.holes.push_back(signedArea(h) > 0.0 ? reversed(h) : h);
    return out;
}

namespace {

/// First moments (times 6) and doubled signed area of a ring, relative to `o`.
struct Moments {
    double twiceArea = 0, mx = 0, my = 0;
};

Moments moments(const Polygon2& p, Vec2 o) noexcept {
    Moments m;
    const std::size_t n = p.size();
    for (std::size_t i = 0; i < n; ++i) {
        const Vec2 a = p[i] - o, b = p[(i + 1) % n] - o;
        const double c = cross(a, b);
        m.twiceArea += c;
        m.mx += (a.x + b.x) * c;
        m.my += (a.y + b.y) * c;
    }
    return m;
}

Vec2 vertexMean(const Polygon2& p) noexcept {
    if (p.empty()) return {};
    Vec2 sum;
    for (Vec2 v : p.vertices) sum = sum + (v - p[0]);
    return p[0] + sum * (1.0 / static_cast<double>(p.size()));
}

} // namespace

Vec2 centroid(const Polygon2& p) noexcept {
    if (p.size() < 3 || winding(p) == Winding::Degenerate) return vertexMean(p);
    const Vec2 o = p[0];
    const Moments m = moments(p, o);
    return o + Vec2{m.mx, m.my} * (1.0 / (3.0 * m.twiceArea));
}

Vec2 centroid(const PolygonWithHoles& p) noexcept {
    if (p.outer.size() < 3 || winding(p.outer) == Winding::Degenerate) return vertexMean(p.outer);
    const Vec2 o = p.outer[0];
    Moments total = moments(p.outer, o);
    const double outerSign = total.twiceArea < 0.0 ? -1.0 : 1.0;
    total = {total.twiceArea * outerSign, total.mx * outerSign, total.my * outerSign};
    for (const Polygon2& h : p.holes) {
        if (h.size() < 3) continue;
        const Moments m = moments(h, o);
        const double s = m.twiceArea < 0.0 ? -1.0 : 1.0; // make the hole contribution positive, then subtract
        total.twiceArea -= m.twiceArea * s;
        total.mx -= m.mx * s;
        total.my -= m.my * s;
    }
    if (total.twiceArea <= 0.0) return centroid(p.outer);
    return o + Vec2{total.mx, total.my} * (1.0 / (3.0 * total.twiceArea));
}

Rect2 bounds(const Polygon2& p) noexcept {
    Rect2 r;
    for (Vec2 v : p.vertices) r.expand(v);
    return r;
}

PointLocation locatePoint(const Polygon2& p, Vec2 q, double eps) noexcept {
    const std::size_t n = p.size();
    if (n == 0) return PointLocation::Outside;
    for (std::size_t i = 0; i < n; ++i)
        if (pointOnSegment(q, p.edge(i), eps)) return PointLocation::OnBoundary;
    if (n < 3) return PointLocation::Outside;
    // Even-odd ray cast towards +X with the half-open rule (a vertex exactly at q.y counts
    // for the edge whose other end is above); boundary cases were handled above.
    bool inside = false;
    for (std::size_t i = 0; i < n; ++i) {
        const Vec2 a = p[i], b = p[(i + 1) % n];
        if ((a.y > q.y) == (b.y > q.y)) continue;
        const double x = a.x + (q.y - a.y) / (b.y - a.y) * (b.x - a.x);
        if (x > q.x) inside = !inside;
    }
    return inside ? PointLocation::Inside : PointLocation::Outside;
}

PointLocation locatePoint(const PolygonWithHoles& p, Vec2 q, double eps) noexcept {
    const PointLocation outer = locatePoint(p.outer, q, eps);
    if (outer != PointLocation::Inside) return outer;
    for (const Polygon2& h : p.holes) {
        const PointLocation l = locatePoint(h, q, eps);
        if (l == PointLocation::OnBoundary) return PointLocation::OnBoundary;
        if (l == PointLocation::Inside) return PointLocation::Outside;
    }
    return PointLocation::Inside;
}

bool isSimple(const Polygon2& p, double eps) noexcept {
    const std::size_t n = p.size();
    if (n < 3) return false;
    for (std::size_t i = 0; i < n; ++i) {
        const Segment2 e = p.edge(i);
        const double len = length(e.b - e.a);
        if (len <= eps || len == 0.0) return false;
    }
    for (std::size_t i = 0; i < n; ++i) {
        const Segment2 ei = p.edge(i);
        // Adjacent edge i+1 shares vertex ei.b: it must not fold back onto edge i.
        const Segment2 next = p.edge((i + 1) % n);
        if (pointOnSegment(next.b, ei, eps) || pointOnSegment(ei.a, next, eps)) return false;
        for (std::size_t j = i + 2; j < n; ++j) {
            if (i == 0 && j == n - 1) continue; // adjacent through the closing vertex
            if (intersectSegments(ei, p.edge(j), eps).kind != SegmentIntersectionKind::None) return false;
        }
    }
    return true;
}

Polygon2 cleaned(const Polygon2& p, double eps) {
    // 1. Drop repeated consecutive vertices.
    std::vector<Vec2> pts;
    pts.reserve(p.size());
    for (Vec2 v : p.vertices)
        if (pts.empty() || length(v - pts.back()) > eps) pts.push_back(v);
    while (pts.size() > 1 && length(pts.front() - pts.back()) <= eps) pts.pop_back();

    // 2. Drop collinear vertices until none is left (removing one can expose another).
    bool changed = true;
    while (changed && pts.size() >= 3) {
        changed = false;
        std::vector<Vec2> kept;
        kept.reserve(pts.size());
        const std::size_t n = pts.size();
        for (std::size_t i = 0; i < n; ++i) {
            const Vec2 prev = kept.empty() ? pts[n - 1] : kept.back();
            const Vec2 next = pts[(i + 1) % n];
            const bool duplicate = length(pts[i] - prev) <= eps;
            if (duplicate || orientation(prev, next, pts[i], eps) == Orientation::Collinear) {
                changed = true;
                continue;
            }
            kept.push_back(pts[i]);
        }
        pts = std::move(kept);
        while (pts.size() > 1 && length(pts.front() - pts.back()) <= eps) pts.pop_back();
    }
    if (pts.size() == 2 && length(pts[0] - pts[1]) <= eps) pts.pop_back();
    return {std::move(pts)};
}

bool isConvex(const Polygon2& p, double eps) {
    const Polygon2 c = cleaned(p, eps);
    const std::size_t n = c.size();
    if (n < 3) return false;
    int sign = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const Orientation o = orientation(c[i], c[(i + 1) % n], c[(i + 2) % n], eps);
        const int s = static_cast<int>(o);
        if (s == 0) continue;
        if (sign == 0) sign = s;
        else if (s != sign) return false;
    }
    // Same-direction turns also occur in star polygons (pentagram): require simplicity.
    return sign != 0 && isSimple(c, eps);
}

Polygon2 convexHull(const std::vector<Vec2>& points, double eps) {
    std::vector<Vec2> pts = points;
    std::sort(pts.begin(), pts.end(), [](Vec2 a, Vec2 b) { return a.x < b.x || (a.x == b.x && a.y < b.y); });
    pts.erase(std::unique(pts.begin(), pts.end()), pts.end());
    if (pts.size() < 3) return cleaned({std::move(pts)}, eps);

    std::vector<Vec2> hull;
    hull.reserve(pts.size() + 1);
    const auto add = [&hull, eps](Vec2 q, std::size_t floor) {
        while (hull.size() >= floor + 2 &&
               orientation(hull[hull.size() - 2], hull.back(), q, eps) != Orientation::CounterClockwise)
            hull.pop_back();
        hull.push_back(q);
    };
    for (Vec2 q : pts) add(q, 0); // lower chain, left to right
    const std::size_t lower = hull.size();
    for (std::size_t i = pts.size() - 1; i-- > 0;) add(pts[i], lower - 1); // upper chain, right to left
    hull.pop_back();                                                      // first point repeated
    return cleaned({std::move(hull)}, eps);
}

Polygon2 translated(const Polygon2& p, Vec2 offset) {
    Polygon2 out = p;
    for (Vec2& v : out.vertices) v = v + offset;
    return out;
}

Polygon2 rotated(const Polygon2& p, double degrees, Vec2 pivot) {
    const double c = std::cos(core::degToRad(degrees)), s = std::sin(core::degToRad(degrees));
    Polygon2 out = p;
    for (Vec2& v : out.vertices) {
        const Vec2 d = v - pivot;
        v = pivot + Vec2{d.x * c - d.y * s, d.x * s + d.y * c};
    }
    return out;
}

Polygon2 mirroredX(const Polygon2& p, double axisX) {
    Polygon2 out = p;
    for (Vec2& v : out.vertices) v.x = 2.0 * axisX - v.x;
    return reversed(out);
}

Polygon2 mirroredY(const Polygon2& p, double axisY) {
    Polygon2 out = p;
    for (Vec2& v : out.vertices) v.y = 2.0 * axisY - v.y;
    return reversed(out);
}

} // namespace fos::geometry
