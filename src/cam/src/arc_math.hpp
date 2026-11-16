// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
// Internal: exact circular-arc helpers shared by the generator, the simulator and the heightmap.
#include "fos/geometry/contour.hpp"
#include "fos/geometry/vec.hpp"

namespace fos::cam::detail {

/// A circular arc in the XY plane: centre, radius, start angle and SIGNED sweep (radians,
/// > 0 counter-clockwise).
struct ArcGeom {
    geometry::Vec2 centre;
    double radius = 0;
    double startAngle = 0;
    double sweep = 0;
};

/// Arc from `start` to `end` around `centre` turning ccw / cw. The radius is |start - centre|;
/// the sweep lies in (0, 2 pi] (sign by direction); start == end gives a full turn.
ArcGeom arcFromEndpoints(geometry::Vec2 start, geometry::Vec2 end, geometry::Vec2 centre, bool ccw) noexcept;
/// Point at parameter s in [0, 1].
geometry::Vec2 arcPoint(const ArcGeom& a, double s) noexcept;
double arcLength(const ArcGeom& a) noexcept;
/// Exact axis-aligned bounds of the arc (extreme points included).
void arcBounds(const ArcGeom& a, double& x0, double& y0, double& x1, double& y1) noexcept;
/// Exact distance from p to the arc (not the full circle).
double distanceToArc(geometry::Vec2 p, const ArcGeom& a) noexcept;
double distanceToSegment(geometry::Vec2 p, geometry::Vec2 a, geometry::Vec2 b) noexcept;
/// Exact distance from p to the contour (minimum over its line and arc segments).
double distanceToContour(geometry::Vec2 p, const geometry::Contour& c) noexcept;
/// The arc of a contour segment (precondition: segmentInfo(c, i).isArc).
ArcGeom segmentArc(const geometry::Contour& c, std::size_t i) noexcept;

} // namespace fos::cam::detail
