// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/domain/part.hpp"
#include "fos/geometry/contour.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

/// Material of a contoured (ADR-0008) flat part in its part-frame XY plane: the outer
/// contour minus the through cut-outs. All lengths in mm. Exact line/arc geometry
/// (`fos/geometry/curve_intersect.hpp`); nothing is tessellated here.
///
/// Used by `machining::validateOperations` (operations must lie in the material) and by the
/// validation module (exact outline interference).
namespace fos::machining {

/// Outer contour and cut-outs, all counter-clockwise. The material is the closed set
/// "inside or on `outer`, and not strictly inside any cut-out".
struct MaterialRegion {
    geometry::Contour outer;
    std::vector<geometry::Contour> cutouts;
};

/// The FINISHED material region of a part with a non-rectangular shape: `shape->outer`
/// and `shape->cutouts`, orientation normalised to counter-clockwise (cut-out order kept).
/// nullopt when the part has no shape, when `domain::isRectangularOutline(part)` (it then
/// behaves exactly like a plain panel), or when the outer contour or a cut-out fails
/// `geometry::validateContour` (the validation module reports such shapes).
std::optional<MaterialRegion> shapedMaterialRegion(const domain::Part& part);

enum class MaterialLocation {
    Material,       ///< Strictly inside the material (farther than eps from every boundary).
    Boundary,       ///< Within eps of the outer contour or of a cut-out.
    OutsideOutline, ///< Outside the outer contour.
    InCutout        ///< Strictly inside cut-out `cutout`.
};

struct PointInMaterial {
    MaterialLocation location = MaterialLocation::OutsideOutline;
    std::size_t cutout = 0; ///< 0-based cut-out index (InCutout, or Boundary of a cut-out).
    bool onCutout = false;  ///< Boundary: true when the nearest boundary is a cut-out.
};

PointInMaterial locateInMaterial(const MaterialRegion& region, geometry::Vec2 p, double eps = core::kLengthEps);

/// Exact distance from `p` to the nearest segment of the outer contour or of any cut-out.
struct BoundaryDistance {
    double distance = 0;
    bool onCutout = false;  ///< Nearest segment belongs to a cut-out.
    std::size_t cutout = 0; ///< 0-based, when `onCutout`.
    std::size_t segment = 0;
};
BoundaryDistance distanceToBoundary(const MaterialRegion& region, geometry::Vec2 p);

/// True when some segment of `a` and some segment of `b` come within `eps` of each other
/// (crossing, touching or overlapping). Exact (geometry::intersectContourSegments).
bool contoursTouch(const geometry::Contour& a, const geometry::Contour& b, double eps = core::kLengthEps);

/// True when the two closed material regions share at least one point (boundaries within
/// eps count as shared). Exact: boundary contact test, then one point of each region is
/// located in the other (sufficient because each region is connected).
bool regionsIntersect(const MaterialRegion& a, const MaterialRegion& b, double eps = core::kLengthEps);

/// Where the footprint of an operation lies relative to the material.
enum class FootprintPlacement {
    Inside,          ///< Completely inside the material, touching no boundary.
    CrossesOutline,  ///< Touches or crosses the outer contour.
    CrossesCutout,   ///< Touches or crosses cut-out `cutout`.
    EnclosesCutout,  ///< Cut-out `cutout` lies inside the footprint.
    OutsideOutline,  ///< Completely outside the outer contour.
    InCutout         ///< Completely inside cut-out `cutout`.
};
struct FootprintResult {
    FootprintPlacement placement = FootprintPlacement::Inside;
    std::size_t cutout = 0; ///< 0-based.
};

/// Classifies a closed, simple footprint contour (any orientation). Callers that accept
/// footprints touching the boundary shrink them by a small tolerance first.
FootprintResult classifyFootprint(const MaterialRegion& region, const geometry::Contour& footprint,
                                  double eps = core::kLengthEps);

/// English phrase for a result, e.g. "crosses cut-out 1 of the shaped outline" (0-based index).
std::string describe(const FootprintResult& r);

} // namespace fos::machining
