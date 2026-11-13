// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/geometry/aabb.hpp"
#include "fos/geometry/projection.hpp"
#include "fos/geometry/ray.hpp"

#include <array>
#include <optional>

/// Qt-free orbit camera of the 3D viewport. All lengths in mm, angles in degrees, world =
/// project frame (right-handed, Z up). Pixel coordinates are widget-local LOGICAL pixels with
/// the origin at the top-left corner, x to the right, y downwards; pixel (0,0) is the top-left
/// corner of the top-left pixel, so the widget centre is (width/2, height/2).
///
/// The same class drives rendering (viewMatrix()/projectionMatrix()) and CPU picking
/// (rayThroughPixel()), so what is drawn under a pixel is what is picked there.
namespace fos::viewport {

enum class CameraProjection { Perspective, Orthographic };

/// 4x4 matrix, column-major (OpenGL convention): element (row r, column c) is m[c * 4 + r].
using Mat4 = std::array<float, 16>;

class Camera {
public:
    /// Vertical field of view of the perspective projection.
    static constexpr double kFovYDeg = 30.0;

    Camera();

    // --- Orientation -------------------------------------------------------------------
    /// Orientation by yaw (about world +Z) and pitch (elevation), both in degrees.
    /// yaw 0 / pitch 0 = Front view (observer at -Y looking towards +Y). yaw 90 = observer at
    /// +X (Right view). pitch +90 = Top view. Pitch is clamped to [-90, 90].
    ///   towardViewer = (sin(yaw) cos(pitch), -cos(yaw) cos(pitch), sin(pitch))
    ///   right        = (cos(yaw), sin(yaw), 0)
    ///   up           = towardViewer x right
    void setOrientation(double yawDeg, double pitchDeg) noexcept;
    double yaw() const noexcept { return yaw_; }
    double pitch() const noexcept { return pitch_; }
    /// The current view frame (orthonormal, right-handed).
    geometry::ViewBasis basis() const noexcept;
    /// Unit viewing direction (= -basis().towardViewer).
    geometry::Vec3 viewDirection() const noexcept;

    // --- Position ----------------------------------------------------------------------
    void setTarget(geometry::Vec3 target) noexcept { target_ = target; }
    geometry::Vec3 target() const noexcept { return target_; }
    /// Distance eye -> target (mm), clamped to [kMinDistance, kMaxDistance].
    void setDistance(double distance) noexcept;
    double distance() const noexcept { return distance_; }
    /// Eye position = target + towardViewer * distance.
    geometry::Vec3 eye() const noexcept;
    static constexpr double kMinDistance = 1.0;
    static constexpr double kMaxDistance = 1.0e7;

    void setProjection(CameraProjection p) noexcept { projection_ = p; }
    CameraProjection projection() const noexcept { return projection_; }

    /// Viewport size in logical pixels (values < 1 are treated as 1).
    void setViewportSize(int width, int height) noexcept;
    int viewportWidth() const noexcept { return width_; }
    int viewportHeight() const noexcept { return height_; }

    /// Half of the visible height (mm) in the plane through the target perpendicular to the
    /// view direction: distance * tan(fovY / 2). Identical for both projections, which is
    /// what makes perspective and orthographic framing match when switching.
    double halfHeightAtTarget() const noexcept;

    /// Bounding sphere used for near/far planes (the whole scene incl. grid); see matrices.
    void setSceneBounds(const geometry::Aabb& bounds) noexcept { scene_ = bounds; }

    // --- Matrices (for OpenGL) ------------------------------------------------------------
    Mat4 viewMatrix() const noexcept;
    Mat4 projectionMatrix() const noexcept;
    /// Near / far clip distances along the view direction from the eye (mm). For the
    /// orthographic projection `near` may be negative (geometry behind the eye is kept).
    double nearPlane() const noexcept;
    double farPlane() const noexcept;

    // --- Picking / projection -------------------------------------------------------------
    /// Ray through a pixel position in world coordinates, unit direction. Perspective: starts
    /// at the eye. Orthographic: starts on the near plane, parallel to the view direction.
    geometry::Ray3 rayThroughPixel(double px, double py) const noexcept;
    /// Pixel position of a world point; nullopt when the point is not in front of the eye
    /// (perspective only). Inverse of rayThroughPixel for points on the ray.
    std::optional<geometry::Vec2> projectToPixel(geometry::Vec3 world) const noexcept;

    // --- Interaction ------------------------------------------------------------------------
    /// Orbit about the target: yaw -= dx * degPerPixel, pitch += dy * degPerPixel (dragging
    /// right turns the model to the right, dragging down tilts the top towards the viewer).
    void orbit(double dxPixels, double dyPixels, double degPerPixel = 0.4) noexcept;
    /// Pan so that the scene point under the cursor follows the cursor (in the target plane).
    void pan(double dxPixels, double dyPixels) noexcept;
    /// Zoom by `factor` (< 1 = closer) keeping the target-plane point under pixel (px, py)
    /// fixed on screen: target' = P + (target - P) * factor, distance' = distance * factor.
    void zoomAt(double px, double py, double factor) noexcept;

    /// Standard views. The preset values: Front (0,0), Back (180,0), Left (-90,0),
    /// Right (90,0), Top (0,90), Bottom (0,-90), Isometric (45, atan(1/sqrt 2) = 35.264):
    /// observer at front-right-above, towardViewer = (1,-1,1)/sqrt(3).
    enum class Preset { Front, Back, Left, Right, Top, Bottom, Isometric };
    void setPreset(Preset preset) noexcept;

    /// Frame an axis-aligned box (keeps the orientation): target = box centre; the box corners
    /// projected on the view plane fit into the viewport with a 5 % margin. In perspective the
    /// distance additionally adds the box's half depth so that its nearest corner fits too.
    /// An empty box frames a 1 m cube at the origin.
    void fit(const geometry::Aabb& box) noexcept;

private:
    double aspect() const noexcept { return static_cast<double>(width_) / static_cast<double>(height_); }

    double yaw_ = 0, pitch_ = 0;
    geometry::Vec3 target_{0, 0, 0};
    double distance_ = 3000;
    CameraProjection projection_ = CameraProjection::Perspective;
    int width_ = 1, height_ = 1;
    geometry::Aabb scene_;
};

} // namespace fos::viewport
