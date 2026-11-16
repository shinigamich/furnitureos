// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/drawings/part_view.hpp"

#include <string>
#include <vector>

/// Private: part-drawing content of contoured parts (ADR-0008), used by part_view.cpp.
/// `ySign` is +1 for the face A view (v = y) and -1 for the face B view (v = -y).
namespace fos::drawings::detail {

/// True when the part is drawn from its contour (it has a shape, even a rectangular one).
bool drawsContour(const domain::Part& part) noexcept;

/// Exact finished outline (visible lines + arcs, source = part id) and cut-outs (visible:
/// they are through openings; circular cut-outs as a circle with a centre cross).
/// Adds the Warning "drawings.part.invalid_shape" when the outline or a cut-out fails
/// geometry::validateContour (the geometry is drawn anyway).
void drawShape(const domain::Part& part, double ySign, View& view);

/// Baseline coordinate of a cut-out (part frame, mm) and the id of its dimension.
struct CutoutCoord {
    double value = 0;
    std::string id;
};
/// Ordinate positions of the cut-outs from the part origin: circular cut-out k -> centre
/// ("<view>/dim.cutout.x[k]", ".y[k]"); other cut-outs -> bounding box min/max
/// ("<view>/dim.cutout.x[k.min]", "[k.max]", same for y). Cut-outs in shape order.
void cutoutCoordinates(const domain::Part& part, const std::string& viewId, std::vector<CutoutCoord>& xs,
                       std::vector<CutoutCoord>& ys);

/// One RadiusDimension per outer arc ("<view>/dim.radius[<segment>]"), per arc of a
/// non-circular cut-out ("<view>/dim.radius.cutout[<k>.<segment>]") and one diameter per
/// circular cut-out ("<view>/dim.diameter.cutout[<k>]"). The leader meets the arc at its
/// middle (circles: at 45 degrees in view axes).
void addRadiusDimensions(const domain::Part& part, const View& view, double ySign, ViewAnnotations& ann);

/// Edge-band marks along the banded outer segments ("<view>/edge.seg[<segment>]"): straight
/// marks for lines, concentric arc marks for arcs. Appends new band ids to `bands` (legend
/// order, E1 = bands[0]).
void addShapeEdgeMarks(const domain::Part& part, const View& view, double ySign,
                       std::vector<domain::EdgeBandId>& bands, ViewAnnotations& ann);

/// Legend tail for a shaped part: "<n> outline segments, <length> mm" (exact finished lengths).
std::string shapeBandUsage(const domain::Part& part, const domain::EdgeBandId& band, const PartDrawingLabels& labels);

} // namespace fos::drawings::detail
