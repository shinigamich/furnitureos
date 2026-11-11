// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/core/result.hpp"
#include "fos/geometry/polygon.hpp"

#include <array>
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

} // namespace fos::geometry
