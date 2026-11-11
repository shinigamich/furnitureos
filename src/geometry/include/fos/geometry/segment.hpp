// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/geometry/vec.hpp"

/// 2D segment predicates and constructions. All coordinates and tolerances are in mm.
/// Every tolerance `eps` is a DISTANCE (not an area or a parameter): two features closer
/// than `eps` are considered to touch.
namespace fos::geometry {

/// Closed 2D line segment from `a` to `b`. May be degenerate (a == b).
struct Segment2 {
    Vec2 a, b;
    constexpr bool operator==(const Segment2&) const noexcept = default;
};

enum class Orientation { Clockwise = -1, Collinear = 0, CounterClockwise = 1 };

/// Orientation of point `c` relative to the directed line a -> b.
/// CounterClockwise: `c` lies more than `eps` to the left; Clockwise: more than `eps` to the
/// right; Collinear: within `eps` of the infinite line (also when |ab| <= eps).
Orientation orientation(Vec2 a, Vec2 b, Vec2 c, double eps = core::kLengthEps) noexcept;

/// Point of the segment nearest to `p` (an endpoint when the projection falls outside).
Vec2 closestPointOnSegment(Vec2 p, const Segment2& s) noexcept;

/// Euclidean distance from `p` to the closed segment `s`.
double distancePointSegment(Vec2 p, const Segment2& s) noexcept;

/// True when `p` is within `eps` of the closed segment `s`.
bool pointOnSegment(Vec2 p, const Segment2& s, double eps = core::kLengthEps) noexcept;

enum class SegmentIntersectionKind {
    None,   ///< The segments are farther apart than eps everywhere.
    Point,  ///< They meet in a single point (crossing, T-junction, shared endpoint).
    Overlap ///< They are collinear and share a stretch longer than eps.
};

struct SegmentIntersection {
    SegmentIntersectionKind kind = SegmentIntersectionKind::None;
    /// Point: the intersection point (p1 == p0). Overlap: start of the shared stretch.
    Vec2 p0;
    /// Overlap: end of the shared stretch; p0 -> p1 runs in the direction of `s1` (a -> b).
    Vec2 p1;
};

/// Classifies the intersection of two closed segments.
///  * Collinear segments (both endpoints of the shorter one within `eps` of the line through
///    the longer one) give Overlap when the shared stretch is longer than `eps`, Point when
///    they only abut end to end, None when they are apart.
///  * Otherwise a Point is reported when the segments cross or when an endpoint of one lies
///    within `eps` of the other; in the latter case that endpoint is returned exactly.
///  * Degenerate segments (length <= eps) are treated as points.
SegmentIntersection intersectSegments(const Segment2& s1, const Segment2& s2,
                                      double eps = core::kLengthEps) noexcept;

struct ClosestPoints {
    Vec2 onFirst;        ///< Point on the first segment.
    Vec2 onSecond;       ///< Point on the second segment.
    double distance = 0; ///< |onFirst - onSecond|; exactly 0 when the segments intersect.
};

/// Pair of closest points between two closed segments (exact, no tolerance).
ClosestPoints closestPointsSegments(const Segment2& s1, const Segment2& s2) noexcept;

} // namespace fos::geometry
