// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/geometry/aabb.hpp"
#include "fos/geometry/transform.hpp"

#include <array>

/// Oriented boxes and their exact intersection test (separating axis theorem).
/// All lengths in mm. The frame is whatever frame the boxes were built in (the
/// assembly frame for parts of one assembly, the project frame across assemblies).
namespace fos::validation {

/// Oriented box: `center` + sum of s_i * half_i * axes[i], s_i in [-1, 1].
/// `axes` must be orthonormal; `half` holds the half extents along axes[0..2] (x, y, z).
struct Obb {
    geometry::Vec3 center;
    std::array<geometry::Vec3, 3> axes{geometry::Vec3{1, 0, 0}, geometry::Vec3{0, 1, 0}, geometry::Vec3{0, 0, 1}};
    geometry::Vec3 half;
};

/// Box `localBox` (axis-aligned in a child frame) placed by the rigid transform `toFrame`.
Obb makeObb(const geometry::Aabb& localBox, const geometry::Transform& toFrame) noexcept;

/// Full extent (not half) of the box projected onto the unit direction `dir`.
double projectedExtent(const Obb& box, geometry::Vec3 dir) noexcept;

struct ObbOverlap {
    /// True when the boxes share volume: the overlap on every one of the 15 candidate
    /// axes is greater than the tolerance. Touching boxes do not intersect.
    bool intersects = false;
    /// When `intersects`: penetration depth in mm = the smallest overlap over the 15
    /// axes, i.e. the shortest translation that separates the boxes (exact for boxes).
    /// When not: the overlap found on the separating axis (<= tolerance, negative = gap
    /// along that axis; not the Euclidean distance).
    double depth = 0;
    /// Unit axis belonging to `depth`, oriented from box `a` towards box `b`.
    geometry::Vec3 axis;
};

/// Separating-axis test over the 3 + 3 face normals and the 9 edge-edge cross products
/// (near-parallel edge pairs, whose cross product degenerates, are skipped; their axis is
/// already covered by the face normals). On equal overlaps a face normal is preferred.
ObbOverlap intersectObb(const Obb& a, const Obb& b, double tolerance = core::kLengthEps) noexcept;

} // namespace fos::validation
