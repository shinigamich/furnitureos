// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/core/result.hpp"
#include "fos/geometry/polygon.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace fos::geometry {

/// Indices of the three corners of a triangle into the vertex list of the triangulated polygon.
using TriangleIndices = std::array<std::uint32_t, 3>;

/// Ear-clipping triangulation of a simple polygon without holes. O(n^2) typical, O(n^3) worst.
///  * Input: simple polygon (see isSimple), counter-clockwise by convention; a clockwise ring
///    is accepted as well. Collinear vertices are allowed and are all used as triangle
///    corners (no T-junctions against neighbouring geometry).
///  * Output: exactly size() - 2 triangles, each counter-clockwise (regardless of the input
///    winding) with positive area, indices referring to `polygon.vertices`. Deterministic.
///  * Errors: InvalidArgument when the polygon is not simple (fewer than 3 vertices, repeated
///    vertices, self-intersection, zero area).
core::Result<std::vector<TriangleIndices>> triangulate(const Polygon2& polygon, double eps = core::kLengthEps);


/// Upper bound on the number of points (outer + holes) one region may have in
/// triangulateWithHoles(): the algorithm is O(n^2).
inline constexpr std::size_t kMaxTriangulationPoints = 20000;

struct TriangulationWithHoles {
    /// The rings, outer first, then the holes in input order. The outer ring is counter-
    /// clockwise and every hole clockwise (input rings are reversed where needed), so the
    /// region's interior is to the LEFT of every ring edge.
    std::vector<Vec2> points;
    /// ringStart[r] = index in `points` of the first point of ring r; ringStart.back() ==
    /// points.size() (one more entry than rings).
    std::vector<std::size_t> ringStart;
    /// Triangles as indices into `points`, counter-clockwise (positive area), covering the
    /// region (outer minus holes) exactly once. No new points; a point is left out only when
    /// it is a zero-area corner (collinear or spike) the clipping could not otherwise use.
    std::vector<TriangleIndices> triangles;
};

/// Ear clipping with holes: each hole is joined to the outer ring by a bridge edge
/// (D. Eberly, "Triangulation by Ear Clipping": holes in descending order of their
/// rightmost vertex, bridge to the visible vertex found by a ray in +x), then the resulting
/// weakly simple ring is clipped. Input: simple rings, holes strictly inside the outer ring
/// and disjoint. Errors (InvalidArgument), never a wrong mesh: a ring with fewer than 3
/// points or zero area, more than kMaxTriangulationPoints points, no ear found, or triangles
/// whose total area differs from area(outer) - sum area(holes) by more than 1e-9 relative.
core::Result<TriangulationWithHoles> triangulateWithHoles(const Polygon2& outer, const std::vector<Polygon2>& holes);

} // namespace fos::geometry
