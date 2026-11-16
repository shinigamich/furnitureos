// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/drawings/annotation.hpp"

#include <vector>

/// Turns a view (model scale) and its annotations into paper-space primitives.
namespace fos::drawings {

/// Paper-space sizes (mm) of everything that does not scale with the model.
struct DrawingStyle {
    DimensionStyle dimension;
    double hatchSpacing = 2.0;      ///< Distance between section hatch lines.
    double edgeMarkOffset = 1.2;    ///< Edge-band mark: distance inside the outline.
    double edgeMarkWeight = 1.0;    ///< Edge-band mark line weight.
    double edgeCodeHeight = 2.5;    ///< Text height of edge-band codes.
    double grainArrowLength = 30.0; ///< Total length of the grain arrow.
    double leaderShoulder = 2.0;    ///< Horizontal landing of a leader before its text.
    double detailLabelHeight = 3.5;
};

/// Same style with every size multiplied by `factor` (used to produce legible
/// annotations in 1:1 model-space exports, e.g. factor 10 for furniture).
DrawingStyle scaled(const DrawingStyle& style, double factor) noexcept;

/// Renders geometry (visible/hidden lines, circles, arcs, section hatching) and all
/// non-hidden annotations. Order: geometry segments, circles, arcs (exact Arc primitives),
/// hatches, edge-band marks, grain arrows, dimensions, radius/diameter dimensions, notes,
/// leaders, detail markers.
/// Geometry primitives carry `sourceId`; annotation primitives carry `elementId`
/// (= the annotation id) so that a UI can map a click back to an editable element.
std::vector<Primitive> renderView(const View& view, const ViewAnnotations& annotations,
                                  const ViewPlacement& placement, const DrawingStyle& style = {});

/// Primitives of one dimension (extension lines, dimension line, terminators, text).
void renderDimension(const Dimension& d, const ViewPlacement& placement, const DimensionStyle& style,
                     std::vector<Primitive>& out);

/// Primitives of one radius/diameter dimension: leader line, arrow (tip on the arc, pointing
/// at the centre), shoulder of DrawingStyle::leaderShoulder and the text (style.textHeight).
void renderRadiusDimension(const RadiusDimension& d, const ViewPlacement& placement, const DrawingStyle& style,
                           std::vector<Primitive>& out);

} // namespace fos::drawings
