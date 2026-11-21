// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/core/units.hpp"
#include "fos/geometry/aabb.hpp"
#include "fos/geometry/vec.hpp"

#include <array>

/// Axis-aligned section (clipping) plane of the 3D view. Qt-free; display only (it never
/// changes the model). Project frame, mm.
namespace fos::viewport {

enum class SectionAxis { X, Y, Z };

struct SectionPlane {
    SectionAxis axis = SectionAxis::X;
    double position = 0; ///< Plane coordinate along `axis` (mm, project frame).
    /// false: the view keeps the side coordinate <= position (the + side is cut away);
    /// true: it keeps coordinate >= position.
    bool flip = false;

    /// Coordinate of `p` along the plane axis (mm).
    double coordinate(geometry::Vec3 p) const noexcept;
    /// Signed distance (mm): > 0 on the kept side, < 0 on the removed side, 0 on the plane.
    double signedDistance(geometry::Vec3 p) const noexcept;
    /// True when `p` is on the kept side or within `eps` mm of the plane.
    bool keeps(geometry::Vec3 p, double eps = core::kLengthEps) const noexcept { return signedDistance(p) >= -eps; }
    /// Plane equation (a, b, c, d) with a*x + b*y + c*z + d == signedDistance(p); (a, b, c) is
    /// the unit normal pointing into the kept side. Used as gl_ClipDistance input.
    std::array<double, 4> equation() const noexcept;
    /// Unit normal pointing into the kept side.
    geometry::Vec3 keptNormal() const noexcept;
};

/// Where a box lies relative to the plane.
enum class SectionSide {
    Kept,    ///< Every point on the kept side (touching the plane counts as kept).
    Removed, ///< Every point on the removed side or on the plane: not drawn, never picked.
    Cut,     ///< The plane passes through the box interior.
};
/// Classification of an axis-aligned box (empty boxes are Removed). `eps` in mm.
SectionSide classify(const SectionPlane& plane, const geometry::Aabb& box, double eps = core::kLengthEps) noexcept;

} // namespace fos::viewport
