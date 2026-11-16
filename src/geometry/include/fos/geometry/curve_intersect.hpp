// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/geometry/contour.hpp"

#include <optional>
#include <vector>

/// Exact intersections and distances of lines and circular arcs (ADR-0008). All lengths and
/// tolerances in mm; `eps` is a DISTANCE: features closer than eps touch.
namespace fos::geometry {

// ---- Infinite primitives -------------------------------------------------------------------

/// Intersection of the infinite lines p1 + t d1 and p2 + s d2; nullopt when they are parallel
/// (|sin angle| <= 1e-12) or a direction is zero.
std::optional<Vec2> intersectLines(Vec2 p1, Vec2 d1, Vec2 p2, Vec2 d2) noexcept;

/// Intersection of the infinite line p + t d with the full circle (center, radius).
/// Two exact points (ordered along d) when the line cuts the circle, one when it touches it
/// exactly, and - when the line misses the circle by at most `eps` - the foot of the
/// perpendicular from the centre (a tangent contact within tolerance). Empty otherwise.
std::vector<Vec2> intersectLineCircle(Vec2 p, Vec2 d, Vec2 center, double radius,
                                      double eps = core::kLengthEps);

/// Intersection of two full circles. Two exact points when they cut each other (ordered:
/// left of the direction c1 -> c2 first), one point for an exact tangency or for circles
/// that miss each other by at most `eps` (the midpoint of the gap). Empty when they are
/// apart, nested, or concentric (coincident circles are reported empty; see
/// intersectContourSegments for overlap detection).
std::vector<Vec2> intersectCircles(Vec2 c1, double r1, Vec2 c2, double r2, double eps = core::kLengthEps);

// ---- Contour segments (finite lines and arcs, see segmentInfo()) --------------------------

/// Point of the segment nearest to p (exact; arcs not approximated).
Vec2 closestPointOnContourSegment(Vec2 p, const ContourSegmentInfo& s) noexcept;
/// Euclidean distance from p to the segment.
double distancePointContourSegment(Vec2 p, const ContourSegmentInfo& s) noexcept;

enum class CurveIntersectionKind {
    None,   ///< Farther apart than eps everywhere.
    Points, ///< Isolated contact points (crossings, tangencies, endpoints within eps).
    Overlap ///< Collinear lines or arcs of the same circle sharing a stretch longer than eps.
};

struct CurveIntersection {
    CurveIntersectionKind kind = CurveIntersectionKind::None;
    /// Points: the contact points, sorted by (x, y), merged when closer than eps.
    /// Overlap of two lines: start and end of the shared stretch. Overlap of arcs: empty.
    std::vector<Vec2> points;
};

/// Classifies how two segments meet. Contact points are the exact curve intersections plus
/// every segment endpoint lying within eps of the other segment, plus tangencies within eps.
CurveIntersection intersectContourSegments(const ContourSegmentInfo& a, const ContourSegmentInfo& b,
                                           double eps = core::kLengthEps);

} // namespace fos::geometry
