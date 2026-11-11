// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/core/result.hpp"
#include "fos/geometry/polygon.hpp"

namespace fos::geometry {

struct OffsetOptions {
    /// Maximum allowed ratio (miter length / |distance|), where the miter length is the
    /// distance from the original vertex to the mitered corner tip. A right-angle corner has
    /// ratio sqrt(2) ~ 1.414; sharper corners have larger ratios. Corners exceeding the limit
    /// are bevelled: the miter tip is cut off by a straight line perpendicular to the corner
    /// bisector at exactly |distance| from the original vertex (2 vertices instead of 1), so
    /// the offset outline never comes closer than |distance| to the original outline.
    /// Values below 1 behave like 1 (every corner on the offset side bevelled).
    double miterLimit = 2.0;
    /// Geometric tolerance in mm.
    double eps = core::kLengthEps;
};

/// Offsets a simple polygon by `distance` mm: positive = inflate (outwards), negative =
/// deflate (inwards). Every edge is moved parallel to itself by |distance|; corners on the
/// side being offset towards (convex corners when inflating, reflex corners when deflating)
/// get a mitered join, bevelled beyond `options.miterLimit`. This is a miter offset: corners
/// are NOT rounded, so corner tips are farther than |distance| from the original outline.
///
/// Input: any simple polygon, either winding; repeated/collinear vertices are removed first.
/// Output: counter-clockwise polygon without collinear vertices whose first vertex is the
/// image of the first (cleaned) input vertex, or the remaining vertex nearest to it when a
/// convex deflate removed it. distance == 0 returns the cleaned, counter-clockwise input.
///
/// Supported cases (result is exact for the miter offset):
///  * CONVEX polygon, any inflate distance.
///  * CONVEX polygon, any deflate distance: computed as an intersection of half-planes, so
///    edges that vanish are handled; ConstraintViolation error when nothing (no area) remains.
///  * NON-CONVEX simple polygon, inflate or deflate, as long as no edge vanishes or flips and
///    the offset outline stays simple (e.g. an L-shape offset by less than half its notch /
///    arm width).
/// Not supported (returns an error, never a wrong polygon):
///  * Non-convex polygons where the offset makes an edge vanish (ConstraintViolation) or
///    makes the outline self-intersect, e.g. a slot closing up or the shape splitting into
///    several pieces (Unsupported). No clipping/merging of self-intersections is performed.
///  * Non-simple or degenerate input (InvalidArgument).
core::Result<Polygon2> offsetPolygon(const Polygon2& polygon, double distance, const OffsetOptions& options = {});

} // namespace fos::geometry
