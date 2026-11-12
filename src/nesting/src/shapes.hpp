// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
// Internal: part orientations, convex decomposition and convex Minkowski sums.
#include "fos/geometry/polygon.hpp"
#include "fos/nesting/types.hpp"

#include <vector>

namespace fos::nesting::detail {

using geometry::Polygon2;
using geometry::Rect2;
using geometry::Vec2;

/// The placement transform shared by the optimiser and the validator:
/// translated(rotated(mirrored ? mirroredX(outline, 0) : outline, rotationDeg), translation).
Polygon2 transformOutline(const Polygon2& outline, double rotationDeg, bool mirrored, Vec2 translation);

/// One allowed orientation of a part.
struct Orientation {
    double rotationDeg = 0;
    bool mirrored = false;
    Polygon2 transformed;          ///< Original vertices mirrored/rotated, not translated.
    Vec2 offset;                   ///< bounds(transformed).min.
    double w = 0, h = 0;           ///< Bounding-box extents.
    std::vector<Polygon2> pieces;  ///< Convex CCW pieces of (transformed - offset).
};

struct Shape {
    double area = 0;    ///< Outline area.
    double netArea = 0; ///< Outline minus holes.
    std::vector<Orientation> orients; ///< Deduplicated (congruent bounding-box-normalised outlines).
};

/// Builds the shape data of a (valid) part. Orientations whose normalised outline coincides
/// (as a vertex set within 1e-9 mm) with an earlier one are dropped.
Shape buildShape(const Part& part, const std::vector<double>& rotations);

/// Decomposes a simple CCW polygon into convex CCW pieces (ear-clipping triangulation followed
/// by Hertel-Mehlhorn merging: at most 4x the optimal piece count). A convex input gives itself.
std::vector<Polygon2> convexPieces(const Polygon2& ccw);

/// Minkowski sum of two convex polygons (convex hull of all vertex sums), CCW.
Polygon2 minkowskiSumConvex(const Polygon2& a, const Polygon2& b);

} // namespace fos::nesting::detail
