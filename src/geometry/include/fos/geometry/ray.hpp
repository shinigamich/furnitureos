// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/geometry/aabb.hpp"
#include "fos/geometry/mesh.hpp"

#include <cstdint>
#include <optional>

/// Ray casting for viewport picking (mm). Ray parameters `t` are in units of the ray
/// direction's length: point = origin + direction * t. Use a unit direction to get mm.
/// Only t >= 0 counts (the ray starts at its origin). A zero or non-finite direction never hits.
namespace fos::geometry {

struct Ray3 {
    Vec3 origin;
    Vec3 direction{0, 0, 1}; ///< Need not be normalised.
    constexpr Vec3 at(double t) const noexcept { return origin + direction * t; }
};

/// Parameter interval of a ray inside a volume, 0 <= tMin <= tMax.
struct RayRange {
    double tMin = 0; ///< Entry (0 when the origin is inside).
    double tMax = 0; ///< Exit.
};

/// Slab test against an axis-aligned box (boundary inclusive: grazing a face or edge hits).
std::optional<RayRange> intersectRayAabb(const Ray3& ray, const Aabb& box) noexcept;

struct TriangleHit {
    double t = 0;        ///< Ray parameter of the hit.
    double u = 0, v = 0; ///< Barycentric weights of b and c: point = (1-u-v) a + u b + v c.
    bool frontFace = true; ///< True when the ray hits the counter-clockwise (outer) side.
};

/// Moeller-Trumbore ray/triangle test. The triangle boundary is inclusive (barycentric
/// tolerance 1e-9) so that rays through a shared mesh edge are never lost. Rays parallel to
/// the triangle plane miss. With `cullBackFaces` hits from behind (clockwise side) are ignored.
std::optional<TriangleHit> intersectRayTriangle(const Ray3& ray, Vec3 a, Vec3 b, Vec3 c,
                                                bool cullBackFaces = false) noexcept;

struct MeshHit {
    double t = 0;
    std::uint32_t triangle = 0; ///< Triangle index (indices[3*triangle ..]).
    Vec3 point;                 ///< Hit point.
    Vec3 normal;                ///< Unit geometric normal of the triangle (CCW side).
    bool frontFace = true;
};

/// Nearest hit of the ray with any triangle of the mesh (smallest t; the lowest triangle
/// index wins ties). Brute force O(triangles). Triangles with out-of-range indices are skipped.
std::optional<MeshHit> intersectRayMesh(const Ray3& ray, const TriangleMesh& mesh,
                                        bool cullBackFaces = false) noexcept;

struct BoxHit {
    double t = 0;
    BoxFace face = BoxFace::ZMax; ///< Face that was hit.
    Vec3 point;                   ///< Hit point in the frame of the ray (parent frame of `placement`).
    Vec3 localPoint;              ///< Hit point in the box frame, inside [0,size].
    Vec3 normal;                  ///< Outward unit normal of the face in the frame of the ray.
};

/// Nearest hit of the ray with the oriented box that occupies [0,size] in its own frame and is
/// placed by the rigid transform `placement` (box frame -> ray frame), e.g. a part with
/// Part::placement. A ray starting outside hits the entry face; a ray starting inside hits
/// the exit face. When the entry point lies on an edge/corner the face of the lower axis
/// (X before Y before Z) is reported. No hit when a size component is not > 0.
std::optional<BoxHit> intersectRayOrientedBox(const Ray3& ray, Vec3 size, const Transform& placement) noexcept;

} // namespace fos::geometry
