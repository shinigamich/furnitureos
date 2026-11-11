// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/core/result.hpp"
#include "fos/geometry/polygon.hpp"
#include "fos/geometry/transform.hpp"

/// Orthographic (parallel) projection for drawings and view-aligned picking. Scale 1:1 (mm).
namespace fos::geometry {

/// Right-handed orthonormal view frame expressed in the frame of the projected geometry:
/// `right` = +X of the picture, `up` = +Y of the picture, `towardViewer` = right x up
/// (points from the scene to the observer; the viewing direction is -towardViewer).
struct ViewBasis {
    Vec3 right{1, 0, 0};
    Vec3 up{0, 0, 1};
    Vec3 towardViewer{0, -1, 0};

    /// True when the three vectors are unit length, mutually perpendicular and right-handed.
    bool isOrthonormal(double eps = 1e-9) const noexcept;
    bool operator==(const ViewBasis&) const noexcept = default;
};

/// Standard views of the assembly frame (X = width, Y = depth with the front at y = 0, Z = up).
ViewBasis frontView() noexcept;  ///< Observer in front (-Y) looking towards +Y: right +X, up +Z.
ViewBasis backView() noexcept;   ///< Observer behind (+Y): right -X, up +Z.
ViewBasis leftView() noexcept;   ///< Observer at -X looking towards +X: right -Y, up +Z.
ViewBasis rightView() noexcept;  ///< Observer at +X looking towards -X: right +Y, up +Z.
ViewBasis topView() noexcept;    ///< Observer above looking down: right +X, up +Y (front at the bottom).
ViewBasis bottomView() noexcept; ///< Observer below looking up: right +X, up -Y.

/// Builds a view basis from the direction towards the observer and an approximate up
/// direction (neither needs to be normalised): towardViewer = normalised input,
/// right = normalised(upHint x towardViewer), up = towardViewer x right.
/// Error InvalidArgument when a vector is zero/non-finite or they are parallel.
core::Result<ViewBasis> makeViewBasis(Vec3 towardViewer, Vec3 upHint);

struct ProjectedPoint {
    Vec2 position;    ///< Picture coordinates in mm: (dot(p, right), dot(p, up)).
    double depth = 0; ///< dot(p, towardViewer) in mm: LARGER = CLOSER to the observer.
};

/// Orthographic projection of a point. `view` must be orthonormal for distances to be preserved.
ProjectedPoint project(const ViewBasis& view, Vec3 point) noexcept;

/// Depth interval covered by an object (nearest = largest depth).
struct DepthRange {
    double farthest = 0;
    double nearest = 0;
};

/// 2D silhouette of the oriented box [0,size] placed by `placement` (box frame -> frame of the
/// view basis): convex hull of the 8 projected corners, counter-clockwise, without collinear
/// points: a hexagon in general, a rectangle (4 vertices) when the view is along a box axis.
/// Fewer than 3 vertices when the projection has no area (a size component is 0 edge-on).
Polygon2 projectBoxSilhouette(const ViewBasis& view, Vec3 size, const Transform& placement,
                              double eps = core::kLengthEps);

/// Depth range of the same oriented box over its 8 corners.
DepthRange projectBoxDepthRange(const ViewBasis& view, Vec3 size, const Transform& placement) noexcept;

} // namespace fos::geometry
