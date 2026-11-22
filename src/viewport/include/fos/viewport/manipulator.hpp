// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Qt-free math of the resize handles (direct modelling, phase 15). The application describes
/// what may be dragged (an item's envelope box, its frame and one handle per draggable face);
/// the viewport shows the handles at the face centres and turns a mouse drag into a new
/// dimension value. Nothing here changes the model. Lengths in mm, world = project frame,
/// pixels as in camera.hpp (widget-local logical pixels, origin top-left).
#include "fos/geometry/aabb.hpp"
#include "fos/geometry/ray.hpp"
#include "fos/geometry/transform.hpp"
#include "fos/viewport/camera.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace fos::viewport {

/// Axis of the target's own frame (`ResizeTarget::frame`).
enum class HandleAxis { X = 0, Y = 1, Z = 2 };

/// One draggable face.
struct ResizeHandleSpec {
    HandleAxis axis = HandleAxis::X;
    bool negative = false; ///< Face at the minimum of `axis` (dragging it moves away along -axis).
    double value = 0;      ///< Current dimension (mm) shown and changed by the drag.
    std::string label;     ///< Dimension letter for the in-view label, UTF-8 (e.g. "W").
};

/// What can be resized: `box` in the target's frame, `frame` maps it to the project frame.
struct ResizeTarget {
    std::string itemId;
    geometry::Transform frame;
    geometry::Aabb box;
    std::vector<ResizeHandleSpec> handles;
};

/// World position of handle `i`: the centre of its face of `box`, transformed by `frame`.
/// Precondition: i < target.handles.size().
geometry::Vec3 handleAnchor(const ResizeTarget& target, std::size_t i) noexcept;

/// Outward unit direction of handle `i` in the world (frame axis, negated for negative faces).
geometry::Vec3 handleDirection(const ResizeTarget& target, std::size_t i) noexcept;

/// Parameter s of the point origin + s * direction (direction unit length) on the axis line that
/// is closest to the ray (closest points of two lines). With w = ray.origin - origin,
/// a = d.d, b = d.u, c = u.u, p = d.w, q = u.w (d = ray direction, u = axis direction):
///   s = (a q - b p) / (a c - b^2).
/// nullopt when the ray is (nearly) parallel to the axis (sin^2 of the angle < 1e-6), where
/// the closest point is undefined.
std::optional<double> closestAxisParameter(const geometry::Ray3& ray, geometry::Vec3 origin,
                                           geometry::Vec3 direction) noexcept;

/// `value` rounded to the nearest multiple of `step` (halves away from zero); `step` <= 0 or
/// non-finite returns `value` unchanged.
double snapToStep(double value, double step) noexcept;

/// New dimension of a drag: snapToStep(startValue + (s - s0), step), at least `minimum`
/// (s0, s = closestAxisParameter at press and now, along the handle's OUTWARD direction, so
/// dragging outwards always enlarges).
double dragValue(double startValue, double s0, double s, double step, double minimum) noexcept;

/// The box (target frame) with handle i's face moved so the dimension along its axis is
/// `newValue`, the opposite face fixed: positive face max = min + newValue, negative face
/// min = max - newValue.
geometry::Aabb ghostBox(const ResizeTarget& target, std::size_t i, double newValue) noexcept;

/// Handle whose projected anchor is within `radiusPx` of pixel (px, py), nearest first, lowest
/// index on ties; nullopt when none (or the anchor is behind the eye).
std::optional<std::size_t> handleAtPixel(const ResizeTarget& target, const Camera& camera, double px, double py,
                                         double radiusPx) noexcept;

} // namespace fos::viewport
