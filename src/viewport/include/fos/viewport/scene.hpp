// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/core/diagnostic.hpp"
#include "fos/domain/model.hpp"
#include "fos/geometry/ray.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

/// Qt-free display scene of the viewport: the parts of a domain::Model flattened into the
/// project frame (mm, Z up), their display state (hidden / isolated / selected), CPU picking
/// and the vertex arrays uploaded to the GPU. Purely presentation data (ARCHITECTURE.md §3).
namespace fos::viewport {

/// One drawable part. `world` = Assembly::placement * Part::placement (part frame -> project).
struct ScenePart {
    std::string id;          ///< domain::PartId::str()
    geometry::Vec3 size;     ///< Finished (length, width, thickness) in mm.
    geometry::Transform world;
    domain::Color color;     ///< Material colour; kFallbackColor when the material is unknown.
    /// True when the part is drawn as the extrusion of its contour (domain::hasShape and not
    /// domain::isRectangularOutline, ADR-0008); false = finished-size box (exactly as before).
    bool contoured = false;
};

/// Colour of parts whose material is missing from the model.
inline constexpr domain::Color kFallbackColor{160, 160, 160};

/// Interleaved vertex for triangles: position (mm), outward normal, part index.
struct MeshVertex {
    float px, py, pz;
    float nx, ny, nz;
    float part; ///< Index into Scene::parts() (exact for < 2^24 parts).
};
/// Vertex for edge lines: position (mm) and part index.
struct LineVertex {
    float px, py, pz;
    float part;
};

class Scene {
public:
    /// Rebuild from a model (parts in model order: assemblies, then parts). Parts with a
    /// non-positive or non-finite size are kept in parts() but produce no geometry and are
    /// never picked.
    void setModel(const domain::Model& model);
    const std::vector<ScenePart>& parts() const noexcept { return parts_; }

    void setHidden(const std::vector<std::string>& ids);
    void setIsolated(const std::vector<std::string>& ids);
    void setSelected(const std::vector<std::string>& ids);

    /// Drawn and pickable: not hidden, and inside the isolation set when that is non-empty,
    /// and with a valid size.
    bool isVisible(std::size_t index) const noexcept;
    bool isSelected(std::size_t index) const noexcept;

    /// Bounds (project frame) of all visible parts; empty when none is visible.
    geometry::Aabb visibleBounds() const;
    /// Bounds of all parts with a valid size regardless of visibility.
    geometry::Aabb allBounds() const;

    /// Nearest visible part hit by the ray, lowest index on ties; nullopt when nothing is hit.
    /// Box parts: geometry::intersectRayOrientedBox. Contoured parts: the ray is moved into the
    /// part frame and intersected with the part's display mesh (geometry::intersectRayMesh), so
    /// a ray through a cut-out or past a rounded corner MISSES the part (accurate to the chord
    /// tolerance of the mesh, see shapeChordTolerance()).
    std::optional<std::size_t> pick(const geometry::Ray3& ray) const;

    /// Display mesh of part `index` in its PART frame: the contour extrusion for contoured parts
    /// (viewport::makeShapedPartGeometry at shapeChordTolerance(L, W)), otherwise
    /// geometry::makeBox(size). Empty for parts without a valid size.
    const geometry::TriangleMesh& localMesh(std::size_t index) const;

    /// Problems found while building the scene, e.g. "viewport.shape.fallback_box" when a
    /// contour cannot be triangulated (the part is then shown and picked as its box).
    const core::Diagnostics& diagnostics() const noexcept { return diagnostics_; }

    /// GPU geometry for ALL parts (visibility/selection are per-part state, not geometry):
    /// box parts give 24 vertices / 36 indices (geometry::makeBox) and 12 edges = 24 line
    /// vertices; contoured parts give their local mesh and feature edges (see
    /// viewport::ShapedPartGeometry), transformed by `world`.
    struct Buffers {
        std::vector<MeshVertex> vertices;
        std::vector<std::uint32_t> indices;
        std::vector<LineVertex> lines;
    };
    Buffers buildBuffers() const;

private:
    std::vector<ScenePart> parts_;
    std::vector<bool> valid_;
    std::vector<geometry::TriangleMesh> meshes_;     // per part, part frame
    std::vector<std::vector<geometry::Edge3>> edges_; // per part, part frame
    core::Diagnostics diagnostics_;
    std::vector<std::string> hidden_, isolated_, selected_; // sorted
};

} // namespace fos::viewport
