// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/drawings/hlr.hpp"
#include "fos/geometry/contour.hpp"

#include <string>
#include <vector>

/// Private: contoured parts (ADR-0008) in assembly views, used by view.cpp.
namespace fos::drawings::detail {

/// Contoured outline that is not a plain rectangle (drawn from its contour in assembly views).
bool isContoured(const domain::Part& part) noexcept;

/// Outline and cut-outs of a FACE-ON contoured part in view coordinates.
struct FaceOnShape {
    std::size_t boxIndex = 0; ///< Index of its occluder (bounding box) in the HLR box list.
    std::string partId, partName;
    geometry::Contour outer;
    std::vector<geometry::Contour> cutouts;
    double depth = 0; ///< Depth of the face nearest to the viewer (where the outline is drawn).
};

/// Projects the finished outline and cut-outs of `part` (axis-aligned placement, thickness
/// along the view direction) into view coordinates.
FaceOnShape makeFaceOnShape(const domain::Part& part, const ViewBasis& basis, double depth, std::size_t boxIndex);

/// Hideable HLR geometry of the shape: lines -> HlrEdge, arcs -> HlrArc (exact).
void appendHlrGeometry(const FaceOnShape& shape, std::vector<HlrEdge>& edges, std::vector<HlrArc>& arcs);

/// "drawings.hlr.shape_approximated" Warnings: one per (face-on contoured part, other part) pair
/// where treating the contoured part as its bounding rectangle hides a stretch of an edge of the
/// other part that is really visible (it lies inside a cut-out or outside the curved outline
/// and no other occluder hides it). Lines are split exactly at the rectangle sides and at the
/// outline/cut-out curves; arcs are checked on a polygon with 0.05 mm chord tolerance (this
/// check only decides whether to warn). Deterministic order.
core::Diagnostics shapeApproximationDiagnostics(const std::string& viewId, const domain::Assembly& assembly,
                                                const std::vector<HlrBox>& boxes,
                                                const std::vector<FaceOnShape>& shapes,
                                                const std::vector<HlrEdge>& edges, const std::vector<HlrArc>& arcs);

} // namespace fos::drawings::detail
