// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/domain/ids.hpp"
#include "fos/domain/operation.hpp"
#include "fos/geometry/aabb.hpp"
#include "fos/geometry/contour.hpp"
#include "fos/geometry/transform.hpp"

#include <array>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace fos::domain {

enum class PartRole {
    Side, Top, Bottom, Shelf, Partition, Back, Door, DrawerFront, DrawerSide, DrawerBack,
    DrawerBottom, Plinth, Worktop, Rail, Generic
};

/// The four narrow edges of a panel, named after the dimension they run along:
///   L1: y = 0   (runs along the length)      L2: y = W
///   W1: x = 0   (runs along the width)       W2: x = L
enum class EdgeSide { L1 = 0, L2 = 1, W1 = 2, W2 = 3 };
inline constexpr std::array<EdgeSide, 4> kAllEdges{EdgeSide::L1, EdgeSide::L2, EdgeSide::W1, EdgeSide::W2};

/// Direction of the visible grain/decor on the part relative to its own frame.
/// `None` means the part may be rotated freely on the sheet.
enum class GrainDirection { None, AlongLength, AlongWidth };

/// Non-rectangular outline of a flat part (ADR-0008), in the part frame's XY plane.
///
/// * `outer` is the FINISHED outline (edge banding included), counter-clockwise, and its exact
///   bounding box is [0, length] x [0, width] of the owning part (validation checks this).
/// * `cutouts` are through openings (sink, cable outlets, handles); each lies strictly inside
///   `outer`. Cut-outs are never edge-banded in this version.
/// * `outerEdges[i]` is the edge band on outer segment i (empty vector = all segments raw;
///   otherwise exactly one entry per segment). When a part has a shape, `Part::edges` must be
///   empty; the per-segment bands replace them.
struct PartShape {
    geometry::Contour outer;
    std::vector<geometry::Contour> cutouts;
    std::vector<std::optional<EdgeBandId>> outerEdges;
    bool operator==(const PartShape&) const = default;
};

/// A flat panel cut from sheet material: rectangular, or with a contour (`shape`).
///
/// DIMENSION CONVENTION (ADR-0004): `length` and `width` are FINISHED dimensions,
/// i.e. they include applied edge banding. The saw (cut) size is derived by
/// cutSize(): banding on W1/W2 shortens the cut length, banding on L1/L2 narrows
/// the cut width. Nothing else in the code base may re-derive this.
struct Part {
    PartId id;
    std::string name;
    PartRole role = PartRole::Generic;
    double length = 0;     ///< Finished size along local X.
    double width = 0;      ///< Finished size along local Y.
    double thickness = 0;  ///< Along local Z; equals the material thickness.
    MaterialId material;
    std::array<std::optional<EdgeBandId>, 4> edges{}; ///< Indexed by EdgeSide.
    GrainDirection grain = GrainDirection::None;
    geometry::Transform placement; ///< Part frame -> owning assembly frame.
    std::vector<Operation> operations;
    std::map<std::string, std::string> custom;
    /// Contoured parts only (ADR-0008); empty for rectangular panels.
    std::optional<PartShape> shape;

    const std::optional<EdgeBandId>& edge(EdgeSide s) const { return edges[static_cast<std::size_t>(s)]; }
    std::optional<EdgeBandId>& edge(EdgeSide s) { return edges[static_cast<std::size_t>(s)]; }

    /// Bounding box in the part's own frame.
    geometry::Aabb localBounds() const { return geometry::Aabb::fromSize({length, width, thickness}); }
    /// Bounding box in the owning assembly's frame.
    geometry::Aabb bounds() const { return localBounds().transformed(placement); }
};

/// Length of the finished edge `s` of the part (L1/L2 -> length, W1/W2 -> width).
double edgeLength(const Part& part, EdgeSide s) noexcept;

const char* toString(PartRole r) noexcept;
const char* toString(EdgeSide s) noexcept;
const char* toString(GrainDirection g) noexcept;

} // namespace fos::domain
