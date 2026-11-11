// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/geometry/segment.hpp"

#include <algorithm>

namespace fos::geometry {

namespace {

SegmentIntersection pointResult(Vec2 p) noexcept { return {SegmentIntersectionKind::Point, p, p}; }

/// Distance of `p` from the infinite line through `s` (s must not be degenerate).
double lineDistance(const Segment2& s, double len, Vec2 p) noexcept {
    return std::fabs(cross(s.b - s.a, p - s.a)) / len;
}

} // namespace

Orientation orientation(Vec2 a, Vec2 b, Vec2 c, double eps) noexcept {
    const Vec2 ab = b - a;
    const double len = length(ab);
    if (len <= eps || len == 0.0) return Orientation::Collinear;
    const double dist = cross(ab, c - a) / len; // signed distance of c from line ab
    if (dist > eps) return Orientation::CounterClockwise;
    if (dist < -eps) return Orientation::Clockwise;
    return Orientation::Collinear;
}

Vec2 closestPointOnSegment(Vec2 p, const Segment2& s) noexcept {
    const Vec2 d = s.b - s.a;
    const double dd = dot(d, d);
    if (dd == 0.0) return s.a;
    const double t = dot(p - s.a, d) / dd;
    if (t <= 0.0) return s.a;
    if (t >= 1.0) return s.b;
    return s.a + d * t;
}

double distancePointSegment(Vec2 p, const Segment2& s) noexcept {
    return length(p - closestPointOnSegment(p, s));
}

bool pointOnSegment(Vec2 p, const Segment2& s, double eps) noexcept {
    return distancePointSegment(p, s) <= eps;
}

SegmentIntersection intersectSegments(const Segment2& s1, const Segment2& s2, double eps) noexcept {
    const Vec2 d1 = s1.b - s1.a, d2 = s2.b - s2.a;
    const double l1 = length(d1), l2 = length(d2);

    // Degenerate segments behave like points.
    if (l1 <= eps || l1 == 0.0) return pointOnSegment(s1.a, s2, eps) ? pointResult(s1.a) : SegmentIntersection{};
    if (l2 <= eps || l2 == 0.0) return pointOnSegment(s2.a, s1, eps) ? pointResult(s2.a) : SegmentIntersection{};

    // Collinear: the shorter segment lies on the line through the longer one.
    const bool firstLonger = l1 >= l2;
    const Segment2& ref = firstLonger ? s1 : s2;
    const Segment2& other = firstLonger ? s2 : s1;
    const double refLen = firstLonger ? l1 : l2;
    if (lineDistance(ref, refLen, other.a) <= eps && lineDistance(ref, refLen, other.b) <= eps) {
        const Vec2 u1 = d1 * (1.0 / l1);
        const double ta = dot(s2.a - s1.a, u1), tb = dot(s2.b - s1.a, u1);
        const Vec2 s2lo = ta <= tb ? s2.a : s2.b, s2hi = ta <= tb ? s2.b : s2.a;
        const double tmin = std::min(ta, tb), tmax = std::max(ta, tb);
        const double lo = std::max(0.0, tmin), hi = std::min(l1, tmax);
        if (hi < lo - eps) return {};
        // Prefer exact input endpoints over recomputed points.
        const Vec2 pLo = tmin <= 0.0 ? s1.a : s2lo;
        const Vec2 pHi = tmax >= l1 ? s1.b : s2hi;
        if (hi - lo <= eps) return pointResult(tmin <= 0.0 ? pHi : pLo);
        return {SegmentIntersectionKind::Overlap, pLo, pHi};
    }

    // Endpoint contact (reported with the exact endpoint).
    if (pointOnSegment(s2.a, s1, eps)) return pointResult(s2.a);
    if (pointOnSegment(s2.b, s1, eps)) return pointResult(s2.b);
    if (pointOnSegment(s1.a, s2, eps)) return pointResult(s1.a);
    if (pointOnSegment(s1.b, s2, eps)) return pointResult(s1.b);

    // Proper crossing.
    const double denom = cross(d1, d2);
    if (denom == 0.0) return {};
    const Vec2 w = s2.a - s1.a;
    const double t = cross(w, d2) / denom;
    const double u = cross(w, d1) / denom;
    if (t < 0.0 || t > 1.0 || u < 0.0 || u > 1.0) return {};
    return pointResult(s1.a + d1 * t);
}

ClosestPoints closestPointsSegments(const Segment2& s1, const Segment2& s2) noexcept {
    const SegmentIntersection x = intersectSegments(s1, s2, 0.0);
    if (x.kind != SegmentIntersectionKind::None) return {x.p0, x.p0, 0.0};
    // Disjoint segments: the minimum is attained at an endpoint of one of them.
    ClosestPoints best{s1.a, closestPointOnSegment(s1.a, s2), 0.0};
    best.distance = length(best.onFirst - best.onSecond);
    const auto consider = [&best](Vec2 onFirst, Vec2 onSecond) {
        const double d = length(onFirst - onSecond);
        if (d < best.distance) best = {onFirst, onSecond, d};
    };
    consider(s1.b, closestPointOnSegment(s1.b, s2));
    consider(closestPointOnSegment(s2.a, s1), s2.a);
    consider(closestPointOnSegment(s2.b, s1), s2.b);
    return best;
}

} // namespace fos::geometry
