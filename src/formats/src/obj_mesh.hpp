// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
// Private: triangulation of a planar region with holes for the OBJ export of shaped parts
// (cap faces of the extruded outline). Not installed.
#include "fos/core/result.hpp"
#include "fos/geometry/polygon.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace fos::formats::detail {

/// Upper bound on the number of points (outer + holes) one region may have: the
/// triangulation is O(n^2).
inline constexpr std::size_t kMaxCapPoints = 20000;

struct CapTriangulation {
    /// The rings, outer first, then the holes in input order. The outer ring is counter-
    /// clockwise and every hole clockwise (input rings are reversed where needed), so the
    /// region's interior is to the LEFT of every ring edge.
    std::vector<geometry::Vec2> points;
    /// ringStart[r] = index in `points` of the first point of ring r; ringStart.back() ==
    /// points.size() (one more entry than rings).
    std::vector<std::size_t> ringStart;
    /// Triangles as indices into `points`, counter-clockwise (positive area), covering the
    /// region (outer minus holes) exactly once. No new points; a point is left out only when
    /// it is a zero-area corner (collinear or spike) the clipping could not otherwise use.
    std::vector<std::array<std::uint32_t, 3>> triangles;
};

/// Ear clipping with holes: each hole is joined to the outer ring by a bridge edge
/// (D. Eberly, "Triangulation by Ear Clipping": holes in descending order of their
/// rightmost vertex, bridge to the visible vertex found by a ray in +x), then the resulting
/// weakly simple ring is clipped. Input: simple rings, holes strictly inside the outer ring
/// and disjoint. Errors (InvalidArgument), never a wrong mesh: a ring with fewer than 3
/// points or zero area, more than kMaxCapPoints points, no ear found, or triangles whose
/// total area differs from area(outer) - sum area(holes) by more than 1e-9 relative.
core::Result<CapTriangulation> triangulateWithHoles(const geometry::Polygon2& outer,
                                                    const std::vector<geometry::Polygon2>& holes);

} // namespace fos::formats::detail
