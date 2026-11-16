// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/drawings/view.hpp"
#include "fos/geometry/contour.hpp"

#include <string>
#include <vector>

/// Private helpers: exact projection of part-frame contours (ADR-0008) into view coordinates.
namespace fos::drawings::detail {

/// Affine map of the part-frame XY plane into view coordinates:
///   view = origin + ex * x + ey * y.
/// For axis-aligned placements ex/ey are signed unit view axes (a rotation or a reflection),
/// so circles stay circles with the same radius.
struct PlaneMap {
    Vec2 origin{};
    Vec2 ex{1, 0};
    Vec2 ey{0, 1};
    Vec2 apply(Vec2 p) const noexcept { return origin + ex * p.x + ey * p.y; }
    Vec2 applyVector(Vec2 v) const noexcept { return ex * v.x + ey * v.y; }
    bool mirrors() const noexcept { return ex.x * ey.y - ex.y * ey.x < 0; }
};

/// Angle of `v` in degrees, normalised to [0, 360).
double angleDeg(Vec2 v) noexcept;

/// Image of an ARC segment as a counter-clockwise (in view axes) ViewArc.
ViewArc mapArc(const geometry::ContourSegmentInfo& s, const PlaneMap& m, LineStyle style, const std::string& source);

/// True when every segment of `c` is an arc of the same centre and radius (a full circle,
/// e.g. geometry::circleContour); sets centre and radius (part frame).
bool isCircle(const geometry::Contour& c, Vec2& center, double& radius) noexcept;

/// Image of the whole contour in view coordinates (bulges negated when the map mirrors).
geometry::Contour mapContour(const geometry::Contour& c, const PlaneMap& m);

/// Counter-clockwise ViewArc of an arc segment given in VIEW coordinates.
ViewArc viewArcOf(const geometry::ContourSegmentInfo& s, LineStyle style, const std::string& source);

/// Appends the exact image of contour `c`: lines to `segments`, arcs to `arcs`.
void appendContour(const geometry::Contour& c, const PlaneMap& m, LineStyle style, const std::string& source,
                   std::vector<ViewSegment>& segments, std::vector<ViewArc>& arcs);

} // namespace fos::drawings::detail
