// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/core/result.hpp"
#include "fos/geometry/aabb.hpp"
#include "fos/geometry/polygon.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

/// Triangle meshes for display and picking (mm, right-handed). These meshes are DERIVED
/// presentation geometry; authoritative part geometry lives in the domain model.
namespace fos::geometry {

/// Indexed triangle mesh, flat shaded: vertices are duplicated per face so that every vertex
/// carries the normal of its face. Triangles are counter-clockwise seen from outside.
struct TriangleMesh {
    std::vector<Vec3> positions;        ///< One entry per vertex.
    std::vector<Vec3> normals;          ///< Unit normal per vertex (same size as positions).
    std::vector<std::uint32_t> indices; ///< Three vertex indices per triangle.

    std::size_t triangleCount() const noexcept { return indices.size() / 3; }
    bool empty() const noexcept { return indices.empty(); }
};

/// 3D line segment (wireframe edge).
struct Edge3 {
    Vec3 a, b;
    constexpr bool operator==(const Edge3&) const noexcept = default;
};

/// Faces of a box [0,size]; the order matches the part faces A, B, L1, L2, W1, W2 of
/// ARCHITECTURE.md 4.2 for a box of size (length, width, thickness).
enum class BoxFace : std::uint8_t {
    ZMax = 0, ///< z = size.z, normal +Z (part face A)
    ZMin = 1, ///< z = 0,      normal -Z (part face B)
    YMin = 2, ///< y = 0,      normal -Y (part edge L1)
    YMax = 3, ///< y = size.y, normal +Y (part edge L2)
    XMin = 4, ///< x = 0,      normal -X (part edge W1)
    XMax = 5, ///< x = size.x, normal +X (part edge W2)
};
/// Outward unit normal of a box face in the box frame.
Vec3 boxFaceNormal(BoxFace face) noexcept;

/// Box occupying [0,size.x] x [0,size.y] x [0,size.z]: 24 vertices, 12 triangles, outward
/// normals. Face f (BoxFace order) owns vertices 4f..4f+3 and triangles 2f and 2f+1.
/// Returns an EMPTY mesh when a size component is not finite or not > 0.
TriangleMesh makeBox(Vec3 size);

/// The 12 edges of the box [0,size]: 4 along X, then 4 along Y, then 4 along Z.
/// Empty when a size component is not finite or not > 0.
std::vector<Edge3> makeBoxEdges(Vec3 size);

/// Right prism: `base` (simple polygon in the XY plane, either winding) extruded from z = 0
/// to z = height. With n base vertices: n - 2 bottom-cap triangles (normal -Z), then n - 2
/// top-cap triangles (normal +Z), then 2 triangles per base edge (outward side normals).
/// All base vertices (also collinear ones) are kept, so the mesh is watertight.
/// Errors: InvalidArgument for a non-simple base or a height that is not finite or not > 0.
core::Result<TriangleMesh> makePrism(const Polygon2& base, double height);

/// Feature edges of the prism for wireframe display: n bottom-ring edges, n top-ring edges,
/// then one vertical edge per base vertex that is a real corner (vertical edges at vertices
/// collinear with their neighbours are omitted). Same errors as makePrism.
core::Result<std::vector<Edge3>> makePrismEdges(const Polygon2& base, double height);

/// Mesh mapped by a rigid transform (positions as points, normals as vectors).
TriangleMesh transformed(const TriangleMesh& mesh, const Transform& t);
std::vector<Edge3> transformed(const std::vector<Edge3>& edges, const Transform& t);

/// Bounding box of all vertex positions (empty Aabb for an empty mesh).
Aabb bounds(const TriangleMesh& mesh) noexcept;

/// Signed enclosed volume in mm^3 by the divergence theorem (sum of signed tetrahedra against
/// the origin). Positive for a closed mesh with outward (CCW) triangles; meaningless for open meshes.
double volume(const TriangleMesh& mesh) noexcept;

/// True when the mesh is a closed, consistently oriented 2-manifold: after welding vertices
/// with EXACTLY equal coordinates, every directed edge occurs once and its opposite once
/// (every edge is shared by exactly two triangles with opposite directions). False for an
/// empty mesh, out-of-range indices or degenerate triangles.
bool isWatertight(const TriangleMesh& mesh);

} // namespace fos::geometry
