// SPDX-License-Identifier: AGPL-3.0-only
// Contour geometry: bulge -> arc parameters, area, perimeter, bounds, orientation, point
// location, tessellation and builders (ADR-0008).
#include "fos/geometry/contour.hpp"

#include "contour_internal.hpp"
#include "fos/geometry/curve_intersect.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace fos::geometry {
namespace {

using detail::kTwoPi;

/// Arcs whose sagitta is below this (mm) are treated as straight lines: their centre would be
/// numerically meaningless and the deviation from the chord is far below core::kLengthEps.
constexpr double kStraightSagitta = 1e-12;

} // namespace

ContourSegmentInfo segmentInfo(const Contour& c, std::size_t i) noexcept {
    ContourSegmentInfo s;
    const std::size_t n = c.size();
    if (n == 0) return s;
    i %= n;
    const ContourVertex& v0 = c.vertices[i];
    const ContourVertex& v1 = c.vertices[(i + 1) % n];
    s.start = v0.point;
    s.end = v1.point;
    const Vec2 d = s.end - s.start;
    const double chord = length(d);
    const double b = v0.bulge;
    if (b == 0.0 || !std::isfinite(b) || !(chord > 0) || std::fabs(b) * chord * 0.5 <= kStraightSagitta) {
        s.length = chord;
        return s;
    }
    // theta = 4 atan(b); radius = chord / (2 sin(|theta|/2)) = chord (1 + b^2) / (4 |b|).
    // The centre lies on the chord's perpendicular bisector at signed distance
    // h = chord (1 - b^2) / (4 b) along the LEFT normal of the chord (left for a small CCW arc,
    // right for a small CW arc; the side flips for |b| > 1, i.e. arcs longer than a half circle).
    const Vec2 u = d * (1.0 / chord);
    const Vec2 leftNormal{-u.y, u.x};
    const Vec2 mid = (s.start + s.end) * 0.5;
    s.isArc = true;
    s.center = mid + leftNormal * (chord * (1.0 - b * b) / (4.0 * b));
    s.radius = chord * (1.0 + b * b) / (4.0 * std::fabs(b));
    const double theta = 4.0 * std::atan(b);
    s.startAngle = core::radToDeg(detail::angleOf(s.start - s.center));
    s.sweep = core::radToDeg(theta);
    s.length = s.radius * std::fabs(theta);
    return s;
}

double signedArea(const Contour& c) noexcept {
    const std::size_t n = c.size();
    double twice = 0.0;
    double segments = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const Vec2 a = c.vertices[i].point;
        const Vec2 b = c.vertices[(i + 1) % n].point;
        twice += cross(a, b);
        const ContourSegmentInfo s = segmentInfo(c, i);
        if (s.isArc) {
            // Signed circular segment between chord and arc: r^2/2 (theta - sin theta). A CCW arc
            // (theta > 0) bulges to the right of its chord, i.e. outwards of a CCW contour.
            const double th = detail::sweepRad(s);
            segments += 0.5 * s.radius * s.radius * (th - std::sin(th));
        }
    }
    return 0.5 * twice + segments;
}

double perimeter(const Contour& c) noexcept {
    double sum = 0.0;
    for (std::size_t i = 0; i < c.size(); ++i) sum += segmentInfo(c, i).length;
    return sum;
}

Rect2 bounds(const Contour& c) noexcept {
    Rect2 box;
    for (std::size_t i = 0; i < c.size(); ++i) {
        const ContourSegmentInfo s = segmentInfo(c, i);
        box.expand(s.start);
        if (!s.isArc) continue;
        const double a0 = detail::startRad(s);
        const double sw = detail::sweepRad(s);
        const Vec2 extrema[4] = {{s.center.x + s.radius, s.center.y},
                                 {s.center.x, s.center.y + s.radius},
                                 {s.center.x - s.radius, s.center.y},
                                 {s.center.x, s.center.y - s.radius}};
        for (int k = 0; k < 4; ++k)
            if (detail::angleInSweep(k * 0.5 * core::kPi, a0, sw)) box.expand(extrema[k]);
    }
    return box;
}

Contour reversed(const Contour& c) {
    const std::size_t n = c.size();
    Contour r;
    r.vertices.reserve(n);
    // Keep vertex 0 first: w_k = v_{(n-k) % n}; segment k of the result is original segment
    // (n-k-1) traversed backwards, so its bulge is -b_{(n-k-1) % n}.
    for (std::size_t k = 0; k < n; ++k) {
        const Vec2 p = c.vertices[(n - k) % n].point;
        const double b = c.vertices[(2 * n - k - 1) % n].bulge;
        r.vertices.push_back({p, b == 0.0 ? 0.0 : -b});
    }
    return r;
}

Contour normalizedCcw(const Contour& c) { return signedArea(c) < 0 ? reversed(c) : c; }

Contour translated(const Contour& c, Vec2 offset) {
    Contour r = c;
    for (auto& v : r.vertices) v.point = v.point + offset;
    return r;
}

PointLocation locatePoint(const Contour& c, Vec2 p, double eps) {
    const std::size_t n = c.size();
    for (std::size_t i = 0; i < n; ++i)
        if (distancePointContourSegment(p, segmentInfo(c, i)) <= eps) return PointLocation::OnBoundary;

    // Even-odd crossing test with the ray from p towards +x. Each piece is y-monotone and
    // counted with the half-open rule (y0 > py) != (y1 > py), so shared endpoints count once.
    int crossings = 0;
    auto lineCross = [&](Vec2 a, Vec2 b) {
        if ((a.y > p.y) == (b.y > p.y)) return;
        const double x = a.x + (p.y - a.y) * (b.x - a.x) / (b.y - a.y);
        if (x > p.x) ++crossings;
    };
    for (std::size_t i = 0; i < n; ++i) {
        const ContourSegmentInfo s = segmentInfo(c, i);
        if (!s.isArc) {
            lineCross(s.start, s.end);
            continue;
        }
        // Split the arc at 90 and 270 degrees (its y extrema) into y-monotone pieces, each
        // inside the right (cos >= 0) or the left half of the circle.
        const double a0 = detail::startRad(s);
        const double sw = detail::sweepRad(s);
        const double dir = sw > 0 ? 1.0 : -1.0;
        std::vector<double> cuts; // parameters in (0, |sw|) measured from a0 in travel direction
        for (double ext : {0.5 * core::kPi, 1.5 * core::kPi}) {
            double t = detail::normalizeAngle(dir * (ext - a0));
            for (; t < std::fabs(sw); t += kTwoPi)
                if (t > 0) cuts.push_back(t);
        }
        std::sort(cuts.begin(), cuts.end());
        Vec2 prev = s.start;
        double tPrev = 0.0;
        auto piece = [&](Vec2 q, double tq) {
            if ((prev.y > p.y) != (q.y > p.y)) {
                const double midAngle = a0 + dir * 0.5 * (tPrev + tq);
                const double dy = p.y - s.center.y;
                const double half = std::sqrt(std::max(0.0, s.radius * s.radius - dy * dy));
                const double x = std::cos(midAngle) >= 0 ? s.center.x + half : s.center.x - half;
                if (x > p.x) ++crossings;
            }
            prev = q;
            tPrev = tq;
        };
        for (double t : cuts) {
            const double a = a0 + dir * t;
            // Exact extreme points (top / bottom of the circle).
            const Vec2 q{s.center.x, std::sin(a) > 0 ? s.center.y + s.radius : s.center.y - s.radius};
            piece(q, t);
        }
        piece(s.end, std::fabs(sw));
    }
    return crossings % 2 == 1 ? PointLocation::Inside : PointLocation::Outside;
}

Polygon2 tessellate(const Contour& c, double chordTolerance) {
    // Tolerances below core::kLengthEps are clamped (bounded vertex count).
    const double tol = std::isfinite(chordTolerance) ? std::max(chordTolerance, core::kLengthEps) : core::kLengthEps;
    Polygon2 out;
    for (std::size_t i = 0; i < c.size(); ++i) {
        const ContourSegmentInfo s = segmentInfo(c, i);
        out.vertices.push_back(s.start);
        if (!s.isArc) continue;
        // A chord subtending phi deviates from the arc by the sagitta r (1 - cos(phi/2)); it is
        // <= tol iff phi <= 2 acos(1 - tol/r). Steps are additionally capped at 90 degrees so that
        // even coarse tolerances give a proper polygon (a half circle becomes >= 2 chords).
        double step = tol < s.radius ? 2.0 * std::acos(1.0 - tol / s.radius) : core::kPi;
        step = std::min(step, 0.5 * core::kPi);
        const double sw = detail::sweepRad(s);
        const auto count = static_cast<std::size_t>(std::max(1.0, std::ceil(std::fabs(sw) / step)));
        const double a0 = detail::startRad(s);
        for (std::size_t k = 1; k < count; ++k)
            out.vertices.push_back(
                detail::pointOnCircle(s.center, s.radius, a0 + sw * static_cast<double>(k) / static_cast<double>(count)));
    }
    return out;
}

Contour rectangleContour(double length, double width) {
    return Contour{{{{0, 0}, 0}, {{length, 0}, 0}, {{length, width}, 0}, {{0, width}, 0}}};
}

core::Result<Contour> roundedRectangleContour(double length, double width, const double (&radii)[4]) {
    using core::formatLength;
    if (!std::isfinite(length) || !std::isfinite(width) || length <= core::kLengthEps || width <= core::kLengthEps)
        return core::makeError(core::ErrorCode::InvalidArgument,
                               "A rounded rectangle needs a positive length and width (got " + formatLength(length, 3) +
                                   " x " + formatLength(width, 3) + " mm).",
                               "roundedRectangleContour");
    for (int k = 0; k < 4; ++k)
        if (!std::isfinite(radii[k]) || radii[k] < 0)
            return core::makeError(core::ErrorCode::InvalidArgument,
                                   "Corner radius " + std::to_string(k) + " is " + formatLength(radii[k], 3) +
                                       " mm; corner radii must be finite and >= 0.",
                                   "roundedRectangleContour");
    const double L = length, W = width;
    const double r0 = radii[0], r1 = radii[1], r2 = radii[2], r3 = radii[3];
    struct Side {
        double sum, side;
        const char* name;
    };
    const Side sides[4] = {{r0 + r1, L, "bottom (corners 0 and 1)"},
                           {r1 + r2, W, "right (corners 1 and 2)"},
                           {r2 + r3, L, "top (corners 2 and 3)"},
                           {r3 + r0, W, "left (corners 3 and 0)"}};
    for (const Side& sd : sides)
        if (sd.sum > sd.side + core::kLengthEps)
            return core::makeError(core::ErrorCode::InvalidArgument,
                                   "Corner radii do not fit the " + std::string(sd.name) + " side: " +
                                       formatLength(sd.sum, 3) + " mm of radii on a " + formatLength(sd.side, 3) +
                                       " mm side.",
                                   "roundedRectangleContour");
    // Bulge of a 90 degree counter-clockwise arc: tan(90/4 degrees) = tan(22.5 degrees) = sqrt(2) - 1.
    const double q = std::tan(core::kPi / 8.0);
    // Corner k: arc from its "in" point (on the incoming side) to its "out" point.
    const Vec2 in[4] = {{0, r0}, {L - r1, 0}, {L, W - r2}, {r3, W}};
    const Vec2 out[4] = {{r0, 0}, {L, r1}, {L - r2, W}, {0, W - r3}};
    // Segment order: bottom line, corner 1 arc, right line, corner 2 arc, top line, corner 3 arc,
    // left line, corner 0 arc (sharp corners and zero-length lines are dropped).
    std::vector<ContourVertex> raw;
    for (int k = 0; k < 4; ++k) {
        const int next = (k + 1) % 4;
        raw.push_back({out[k], 0.0});          // straight side to the next corner
        if (radii[next] > 0) raw.push_back({in[next], q}); // corner arc
    }
    Contour c;
    const std::size_t n = raw.size();
    for (std::size_t i = 0; i < n; ++i)
        if (!approxEqual(raw[i].point, raw[(i + 1) % n].point, core::kLengthEps)) c.vertices.push_back(raw[i]);
    return c;
}

Contour circleContour(Vec2 center, double radius) {
    return Contour{{{{center.x + radius, center.y}, 1.0}, {{center.x - radius, center.y}, 1.0}}};
}

} // namespace fos::geometry
