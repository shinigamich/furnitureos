// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// Exact-outline refinement of the box interference test for contoured parts (ADR-0008).
/// Internal to the validation module.
#include "fos/domain/part.hpp"
#include "fos/geometry/transform.hpp"
#include "fos/machining/material_region.hpp"

#include <optional>

namespace fos::validation::detail {

/// A contoured part prepared for the exact test: its finished material region (part frame)
/// and thickness. Only built for shapes that are valid and lie inside [0,L]x[0,W], so that
/// the part's box really encloses its material (the box broad phase stays conservative).
struct ShapeSolid {
    machining::MaterialRegion region;
    double thickness = 0;
};

/// ShapeSolid for a part with a usable non-rectangular shape, nullopt otherwise.
std::optional<ShapeSolid> makeShapeSolid(const domain::Part& part);

struct ShapeOverlap {
    /// False: the exact test could not be evaluated (an erosion of the outline was not
    /// representable at the tolerance); the caller keeps the box result.
    bool decided = false;
    bool intersects = false;
    /// When `intersects`: diameter of the largest ball that fits into the shared material,
    /// capped by the box penetration depth; see README (check 3).
    double depth = 0;
    /// The bisection stopped early because the outline could not be eroded further (an arc
    /// radius smaller than the erosion): `depth` is then a lower bound.
    bool depthIsLowerBound = false;
};

/// Refines a pair whose boxes interfere. `sa`/`sb` are the prepared shapes (nullptr = the
/// part is a plain box), `ta`/`tb` place the parts in a common frame, `tol` is the accepted
/// overlap and `boxDepth` the SAT penetration depth of the boxes.
ShapeOverlap refineShapeOverlap(const domain::Part& a, const geometry::Transform& ta, const ShapeSolid* sa,
                                const domain::Part& b, const geometry::Transform& tb, const ShapeSolid* sb,
                                double tol, double boxDepth);

} // namespace fos::validation::detail
