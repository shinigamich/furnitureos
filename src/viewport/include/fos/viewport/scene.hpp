// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
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

    /// Nearest visible part hit by the ray (geometry::intersectRayOrientedBox), lowest index on
    /// ties; nullopt when nothing is hit.
    std::optional<std::size_t> pick(const geometry::Ray3& ray) const;

    /// GPU geometry for ALL parts (visibility/selection are per-part state, not geometry):
    /// 24 vertices / 36 indices per part (geometry::makeBox) and 12 edges = 24 line vertices.
    struct Buffers {
        std::vector<MeshVertex> vertices;
        std::vector<std::uint32_t> indices;
        std::vector<LineVertex> lines;
    };
    Buffers buildBuffers() const;

private:
    std::vector<ScenePart> parts_;
    std::vector<bool> valid_;
    std::vector<std::string> hidden_, isolated_, selected_; // sorted
};

} // namespace fos::viewport
