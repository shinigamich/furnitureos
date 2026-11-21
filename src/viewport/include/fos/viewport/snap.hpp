// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/geometry/vec.hpp"
#include "fos/viewport/camera.hpp"
#include "fos/viewport/scene.hpp"

#include <cstddef>
#include <optional>
#include <vector>

/// Snapping and distance measurement of the 3D view (Qt-free). Measurements are display
/// only: nothing here reads or writes the domain model. Project frame, mm; pixels are
/// widget-local logical pixels with the origin top-left (as Camera::projectToPixel).
namespace fos::viewport {

/// Kind of a snap point, in tie-break priority order (lower wins at equal pixel distance).
enum class SnapKind {
    Corner,        ///< Corner of a box part.
    OutlineVertex, ///< Corner of a contoured part's outline on face A or B (tangent turn > 1 deg).
    EdgeMidpoint,  ///< Midpoint of a box edge, or of the vertical edge at an outline corner.
    FaceCenter,    ///< Centre of a box face.
    Surface,       ///< Free point on a part surface (or section cap) under the cursor.
};

struct SnapPoint {
    geometry::Vec3 point;            ///< Project frame, mm.
    SnapKind kind = SnapKind::Surface;
    std::size_t part = 0;            ///< Index into Scene::parts().
};

/// Default snap radius around the cursor (logical pixels).
inline constexpr double kSnapRadiusPx = 12.0;

/// Snap candidates of part `index` in the project frame, in a fixed order:
/// * box parts: 8 corners (x fastest, then y, then z of the part frame), 12 edge midpoints
///   (in geometry::makeBoxEdges order), 6 face centres (geometry::BoxFace order);
/// * contoured parts: one OutlineVertex at both ends of every vertical feature edge
///   (ShapedPartGeometry::edges with equal x/y ends) and that edge's EdgeMidpoint.
/// Empty for invisible parts. With a section plane, points on its removed side are dropped.
std::vector<SnapPoint> partSnapPoints(const Scene& scene, std::size_t index);

/// Snap point for the cursor at pixel (px, py) (pass the pixel centre, e.g. x + 0.5):
/// the candidate of any visible part whose projection is nearest to the cursor within
/// `radiusPx`, provided it is not hidden: a ray from the candidate toward the viewer (starting
/// 0.05 mm off it) hits no part (Scene::pickHit, boundary inclusive, so points behind a face
/// seen edge-on also count as hidden); ties: SnapKind order, then part and candidate order. Without such a candidate: the Surface point hit by the cursor ray
/// (Scene::pickHit); nullopt when the cursor is over empty space.
std::optional<SnapPoint> snapAt(const Scene& scene, const Camera& camera, double px, double py,
                                double radiusPx = kSnapRadiusPx);

/// Two-point distance measurement (project frame, mm).
struct Measurement {
    geometry::Vec3 a, b;
    /// b - a: the signed ΔX / ΔY / ΔZ shown to the user.
    geometry::Vec3 delta() const noexcept { return b - a; }
    /// Euclidean distance |b - a| in mm.
    double distance() const noexcept { return geometry::length(b - a); }
};

} // namespace fos::viewport
