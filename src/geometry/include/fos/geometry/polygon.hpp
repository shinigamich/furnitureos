// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/geometry/rect.hpp"
#include "fos/geometry/segment.hpp"

#include <cstddef>
#include <vector>

/// 2D polygons (mm). A polygon is a closed ring of vertices; the closing edge from the last
/// vertex back to the first is implicit (do NOT repeat the first vertex). Unless stated
/// otherwise functions accept either winding. Tolerances `eps` are distances in mm.
namespace fos::geometry {

struct Polygon2 {
    std::vector<Vec2> vertices;

    std::size_t size() const noexcept { return vertices.size(); }
    bool empty() const noexcept { return vertices.empty(); }
    Vec2 operator[](std::size_t i) const noexcept { return vertices[i]; }
    /// Edge i runs from vertex i to vertex (i + 1) % size(). Requires i < size().
    Segment2 edge(std::size_t i) const noexcept { return {vertices[i], vertices[(i + 1) % vertices.size()]}; }
    bool operator==(const Polygon2&) const noexcept = default;
};

/// Polygon with holes. Convention after normalized(): outer ring CCW, holes CW. Holes must lie
/// inside the outer ring and must not overlap each other (not verified).
struct PolygonWithHoles {
    Polygon2 outer;
    std::vector<Polygon2> holes;
    bool operator==(const PolygonWithHoles&) const noexcept = default;
};

enum class Winding { CounterClockwise, Clockwise, Degenerate };

/// Counter-clockwise polygon for the rectangle (4 vertices starting at `r.min`). Empty for an empty rect.
Polygon2 makeRectPolygon(const Rect2& r);

/// Shoelace area in mm^2: positive for CCW, negative for CW rings. 0 for fewer than 3 vertices.
double signedArea(const Polygon2& p) noexcept;
/// |signedArea|.
double area(const Polygon2& p) noexcept;
/// Outer area minus hole areas (independent of ring windings).
double area(const PolygonWithHoles& p) noexcept;
/// Length of the closed boundary in mm.
double perimeter(const Polygon2& p) noexcept;

/// Winding of the ring. Degenerate when it has fewer than 3 vertices or encloses no area
/// (|signedArea| <= eps * perimeter / 2, i.e. the ring is on average thinner than eps).
Winding winding(const Polygon2& p, double eps = core::kLengthEps) noexcept;
/// Same vertices in opposite order (keeps vertex 0 first).
Polygon2 reversed(const Polygon2& p);
/// The ring made counter-clockwise (reversed when it is clockwise; unchanged otherwise).
Polygon2 normalizedCcw(const Polygon2& p);
/// Outer ring CCW, every hole CW.
PolygonWithHoles normalized(const PolygonWithHoles& p);

/// Area centroid. For a degenerate ring (zero area) the mean of the vertices; {0,0} when empty.
Vec2 centroid(const Polygon2& p) noexcept;
/// Area centroid of outer minus holes (falls back to the outer centroid when the net area is 0).
Vec2 centroid(const PolygonWithHoles& p) noexcept;
/// Bounding box of the vertices (empty Rect2 for an empty polygon).
Rect2 bounds(const Polygon2& p) noexcept;

enum class PointLocation { Outside, OnBoundary, Inside };

/// Locates `q` relative to a simple polygon: OnBoundary when within `eps` of an edge,
/// otherwise Inside/Outside by the even-odd rule.
PointLocation locatePoint(const Polygon2& p, Vec2 q, double eps = core::kLengthEps) noexcept;
/// Same for a polygon with holes: a point strictly inside a hole is Outside, a point on a
/// hole's boundary is OnBoundary.
PointLocation locatePoint(const PolygonWithHoles& p, Vec2 q, double eps = core::kLengthEps) noexcept;

/// True when the ring is a simple polygon: at least 3 vertices, no edge shorter than `eps`
/// (no repeated consecutive vertices), adjacent edges do not fold back onto each other and
/// non-adjacent edges stay farther apart than `eps`. O(n^2).
bool isSimple(const Polygon2& p, double eps = core::kLengthEps) noexcept;
/// True when the ring (after cleaned()) is simple and all its turns have the same direction.
bool isConvex(const Polygon2& p, double eps = core::kLengthEps);

/// Removes repeated vertices (closer than `eps`, including last == first) and vertices that
/// are collinear with their neighbours (straight-through points and zero-width spikes).
/// May return fewer than 3 vertices for degenerate input. Keeps winding and vertex order.
Polygon2 cleaned(const Polygon2& p, double eps = core::kLengthEps);

/// Convex hull (Andrew's monotone chain), counter-clockwise, starting at the lowest-x (then
/// lowest-y) point, without collinear or repeated points. Fewer than 3 vertices are returned
/// when all points coincide or are collinear (then: the distinct extreme points).
Polygon2 convexHull(const std::vector<Vec2>& points, double eps = core::kLengthEps);

Polygon2 translated(const Polygon2& p, Vec2 offset);
/// Counter-clockwise rotation by `degrees` about `pivot`.
Polygon2 rotated(const Polygon2& p, double degrees, Vec2 pivot = {});
/// Mirror image across the vertical line x = axisX. The vertex order is reversed so that the
/// winding is preserved (a CCW polygon stays CCW).
Polygon2 mirroredX(const Polygon2& p, double axisX = 0.0);
/// Mirror image across the horizontal line y = axisY; winding preserved as for mirroredX.
Polygon2 mirroredY(const Polygon2& p, double axisY = 0.0);

// ---- Relations between SIMPLE polygons (either winding). Results for non-simple input are
// ---- unspecified (but never crash). O(n * m * (n + m)).

/// True when the interiors intersect. Polygons that only touch (shared edge stretch, vertex
/// on edge, shared vertex) do NOT overlap. Tolerance: penetrations of the order of `eps`
/// (up to a few eps deep) count as touching. Identical polygons overlap; a polygon strictly
/// inside the other overlaps it.
bool polygonsOverlap(const Polygon2& a, const Polygon2& b, double eps = core::kLengthEps);

/// True when `inner` lies entirely inside or on the boundary of `outer` (within `eps`).
/// A polygon contains itself.
bool polygonContainsPolygon(const Polygon2& outer, const Polygon2& inner, double eps = core::kLengthEps);
/// Same for an outer polygon with holes: additionally `inner` must not overlap any hole.
bool polygonContainsPolygon(const PolygonWithHoles& outer, const Polygon2& inner,
                            double eps = core::kLengthEps);

/// Minimum Euclidean distance (mm) between the two filled polygons: 0 when they touch,
/// overlap or one contains the other. +infinity when either polygon is empty.
double polygonDistance(const Polygon2& a, const Polygon2& b);

} // namespace fos::geometry
