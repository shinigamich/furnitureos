// SPDX-License-Identifier: AGPL-3.0-only
// Exact line / circle / arc intersections and distances (ADR-0008).
#include "fos/geometry/curve_intersect.hpp"

#include "contour_internal.hpp"
#include "fos/geometry/segment.hpp"

#include <algorithm>
#include <cmath>

namespace fos::geometry {
namespace {

using detail::kTwoPi;

/// Sorts by (x, y) and drops points closer than eps to an already kept point.
std::vector<Vec2> mergePoints(std::vector<Vec2> pts, double eps) {
    std::sort(pts.begin(), pts.end(), [](Vec2 a, Vec2 b) { return a.x < b.x || (a.x == b.x && a.y < b.y); });
    std::vector<Vec2> out;
    for (Vec2 p : pts) {
        const bool dup = std::any_of(out.begin(), out.end(), [&](Vec2 q) { return length(p - q) <= eps; });
        if (!dup) out.push_back(p);
    }
    return out;
}

/// Angular length (radians) shared by two arcs of the same circle.
double sharedSweep(const ContourSegmentInfo& a, const ContourSegmentInfo& b) noexcept {
    auto ccw = [](const ContourSegmentInfo& s, double& start, double& len) {
        const double sw = detail::sweepRad(s);
        start = detail::startRad(s) + (sw < 0 ? sw : 0.0);
        len = std::fabs(sw);
    };
    double s1, l1, s2, l2;
    ccw(a, s1, l1);
    ccw(b, s2, l2);
    // Arc b starts at offset d within [0, 2pi) of arc a's start; count [d, d+l2] intersected
    // with [0, l1] and with its copy [2pi, 2pi + l1].
    const double d = detail::normalizeAngle(s2 - s1);
    const double first = std::max(0.0, std::min(l1, d + l2) - std::min(l1, d));
    const double second = std::max(0.0, std::min(d + l2, kTwoPi + l1) - kTwoPi);
    return first + second;
}

} // namespace

std::optional<Vec2> intersectLines(Vec2 p1, Vec2 d1, Vec2 p2, Vec2 d2) noexcept {
    const double den = cross(d1, d2);
    const double scale = length(d1) * length(d2);
    if (!(scale > 0) || std::fabs(den) <= 1e-12 * scale) return std::nullopt;
    const double t = cross(p2 - p1, d2) / den;
    return p1 + d1 * t;
}

std::vector<Vec2> intersectLineCircle(Vec2 p, Vec2 d, Vec2 center, double radius, double eps) {
    const double l = length(d);
    if (!(l > 0)) return {};
    const Vec2 u = d * (1.0 / l);
    const Vec2 foot = p + u * dot(center - p, u);
    const double h = length(center - foot);
    if (h > radius + eps) return {};
    if (h >= radius) return {foot};
    const double k = std::sqrt(radius * radius - h * h);
    if (k == 0.0) return {foot};
    return {foot - u * k, foot + u * k};
}

std::vector<Vec2> intersectCircles(Vec2 c1, double r1, Vec2 c2, double r2, double eps) {
    const double dist = length(c2 - c1);
    if (!(dist > 0)) return {};
    const Vec2 u = (c2 - c1) * (1.0 / dist);
    // Distance from c1, along u, of the radical line; h = half the common chord.
    const double a = (dist * dist + r1 * r1 - r2 * r2) / (2.0 * dist);
    const double h2 = r1 * r1 - a * a;
    if (h2 < 0) {
        const double gapOut = dist - (r1 + r2);           // > 0: apart
        const double gapIn = std::fabs(r1 - r2) - dist;   // > 0: nested
        if (std::max(gapOut, gapIn) > eps) return {};
        const Vec2 p = c1 + u * (a >= 0 ? r1 : -r1);
        const Vec2 toP = p - c2;
        const double lp = length(toP);
        const Vec2 q = lp > 0 ? c2 + toP * (r2 / lp) : p;
        return {(p + q) * 0.5};
    }
    const double h = std::sqrt(h2);
    const Vec2 base = c1 + u * a;
    const Vec2 left{-u.y, u.x};
    if (h == 0.0) return {base};
    return {base + left * h, base - left * h};
}

Vec2 closestPointOnContourSegment(Vec2 p, const ContourSegmentInfo& s) noexcept {
    if (!s.isArc) return closestPointOnSegment(p, Segment2{s.start, s.end});
    const Vec2 v = p - s.center;
    const double lv = length(v);
    if (lv > 0 && detail::angleInSweep(detail::angleOf(v), detail::startRad(s), detail::sweepRad(s)))
        return s.center + v * (s.radius / lv);
    return length(p - s.start) <= length(p - s.end) ? s.start : s.end;
}

double distancePointContourSegment(Vec2 p, const ContourSegmentInfo& s) noexcept {
    return length(p - closestPointOnContourSegment(p, s));
}

CurveIntersection intersectContourSegments(const ContourSegmentInfo& a, const ContourSegmentInfo& b, double eps) {
    CurveIntersection r;
    if (!a.isArc && !b.isArc) {
        const SegmentIntersection si = intersectSegments(Segment2{a.start, a.end}, Segment2{b.start, b.end}, eps);
        if (si.kind == SegmentIntersectionKind::Overlap) {
            r.kind = CurveIntersectionKind::Overlap;
            r.points = {si.p0, si.p1};
        } else if (si.kind == SegmentIntersectionKind::Point) {
            r.kind = CurveIntersectionKind::Points;
            r.points = {si.p0};
        }
        return r;
    }
    std::vector<Vec2> candidates{a.start, a.end, b.start, b.end};
    auto add = [&](const std::vector<Vec2>& pts) { candidates.insert(candidates.end(), pts.begin(), pts.end()); };
    if (a.isArc && b.isArc) {
        const bool coincident =
            length(a.center - b.center) <= eps && std::fabs(a.radius - b.radius) <= eps;
        if (coincident) {
            if (sharedSweep(a, b) * std::max(a.radius, b.radius) > eps) {
                r.kind = CurveIntersectionKind::Overlap;
                return r;
            }
        } else {
            add(intersectCircles(a.center, a.radius, b.center, b.radius, eps));
        }
    } else {
        const ContourSegmentInfo& line = a.isArc ? b : a;
        const ContourSegmentInfo& arc = a.isArc ? a : b;
        add(intersectLineCircle(line.start, line.end - line.start, arc.center, arc.radius, eps));
    }
    std::vector<Vec2> hits;
    for (Vec2 p : candidates)
        if (distancePointContourSegment(p, a) <= eps && distancePointContourSegment(p, b) <= eps) hits.push_back(p);
    r.points = mergePoints(std::move(hits), eps);
    if (!r.points.empty()) r.kind = CurveIntersectionKind::Points;
    return r;
}

} // namespace fos::geometry
