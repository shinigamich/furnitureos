// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/core/result.hpp"
#include "fos/geometry/polygon.hpp"
#include "fos/geometry/rect.hpp"
#include "fos/geometry/vec.hpp"

#include <cstddef>
#include <vector>

/// Closed planar contours made of straight segments and circular arcs (ADR-0008).
///
/// CONTRACT fixed by the integration lead; the geometry module implements it. Extend only
/// by adding functions.
///
/// Representation: the DXF LWPOLYLINE "bulge" convention, the de-facto exchange format of
/// furniture CAD and CNC software. A contour is a closed ring of vertices; segment i runs
/// from vertex i to vertex (i+1) % n. `bulge` of vertex i describes segment i:
///   bulge = tan(theta / 4), theta = included angle of the arc,
///   0 = straight line, > 0 = counter-clockwise arc, < 0 = clockwise arc,
///   |bulge| = 1 is a half circle; |bulge| must be < 1e6 (no full circles in one segment;
///   a circle is two half-circle segments).
/// All lengths in mm, angles in degrees at the API. Arcs are exact; tessellation happens only
/// where a consumer needs polygons (rendering, nesting, polygon validation), always with an
/// explicit chord tolerance.
namespace fos::geometry {

struct ContourVertex {
    Vec2 point;
    double bulge = 0.0;
    bool operator==(const ContourVertex&) const = default;
};

struct Contour {
    std::vector<ContourVertex> vertices; ///< Closed; do not repeat the first vertex.
    bool operator==(const Contour&) const = default;
    std::size_t size() const noexcept { return vertices.size(); }
};

/// Geometry of one segment, derived from the bulge.
struct ContourSegmentInfo {
    Vec2 start, end;
    bool isArc = false;
    Vec2 center;          ///< Arcs only.
    double radius = 0;    ///< Arcs only, > 0.
    double startAngle = 0; ///< Degrees, arcs only (direction from center to start).
    double sweep = 0;      ///< Signed degrees: > 0 counter-clockwise.
    double length = 0;     ///< Exact length (arc length for arcs).
};

ContourSegmentInfo segmentInfo(const Contour& c, std::size_t i) noexcept;

/// Structural validity: >= 2 vertices (2 only if at least one segment is an arc), finite
/// coordinates, finite |bulge| < 1e6, no zero-length segments, and the contour does not
/// self-intersect (exact line/arc intersection tests, tolerance `eps`). Errors name the
/// offending segment index.
core::Result<void> validateContour(const Contour& c, double eps = core::kLengthEps);

/// Exact signed area (> 0 for counter-clockwise), including circular segment areas.
double signedArea(const Contour& c) noexcept;
double perimeter(const Contour& c) noexcept;
/// Exact bounding box (arc extrema included).
Rect2 bounds(const Contour& c) noexcept;
/// Same contour traversed the other way (bulges negated and shifted).
Contour reversed(const Contour& c);
/// Counter-clockwise version of `c`.
Contour normalizedCcw(const Contour& c);
Contour translated(const Contour& c, Vec2 offset);

/// Point location against the exact contour (arcs not approximated).
PointLocation locatePoint(const Contour& c, Vec2 p, double eps = core::kLengthEps);

/// Polygon approximation whose maximum distance from every arc is <= chordTolerance (> 0).
/// Vertices of the contour are kept exactly; arc points are added. Orientation is preserved.
Polygon2 tessellate(const Contour& c, double chordTolerance);

/// Builders (all counter-clockwise).
Contour rectangleContour(double length, double width);
/// Rectangle [0,L]x[0,W] with corner radii (r_i >= 0; r_i = 0 keeps a sharp corner),
/// corners ordered (0,0), (L,0), (L,W), (0,W). Error if radii do not fit.
core::Result<Contour> roundedRectangleContour(double length, double width, const double (&radii)[4]);
Contour circleContour(Vec2 center, double radius);

/// Offset with a separate distance per segment (distance[i] applies to segment i):
/// positive = outwards (to the left of a clockwise / right of a counter-clockwise contour,
/// i.e. away from the enclosed area), negative = inwards. Lines stay lines, arcs stay
/// concentric arcs (radius +/- d), neighbours are re-joined at their exact intersection
/// (miter for lines; line/arc and arc/arc intersections solved exactly). Used for
/// edge-banding allowances (finished -> raw contour) and tool-radius compensation.
/// Returns an error (never a wrong contour) when an arc radius would become <= 0, a segment
/// would vanish or reverse, or the result self-intersects.
core::Result<Contour> offsetContour(const Contour& c, const std::vector<double>& distance,
                                    double eps = core::kLengthEps);
/// Uniform offset convenience.
core::Result<Contour> offsetContour(const Contour& c, double distance, double eps = core::kLengthEps);

} // namespace fos::geometry
