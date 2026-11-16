// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/cam/setup.hpp"
#include "fos/core/result.hpp"
#include "fos/domain/model.hpp"
#include "fos/geometry/contour.hpp"

#include <vector>

/// Contour routing targets of a shaped part (ADR-0008), derived from the DOMAIN only.
///
/// A part needs contour routing iff `!domain::isRectangularOutline(part)` (an outline with arcs or
/// non-rectangular corners, or any cut-out). Rectangular parts are cut on the panel saw and get no
/// contour target, so their programs are exactly as before.
///
/// The routed outline is the RAW outline `domain::cutContour(part, model)`: the panel is routed
/// before edge banding is applied. Cut-outs are never banded (ADR-0008) and are routed as given.
/// No new domain operation type is introduced: the targets carry synthetic operation ids
///   "contour.cutout.<k>" (k = index into PartShape::cutouts) and "contour.outline",
/// which must not collide with the part's own operation ids.
namespace fos::cam {

struct ContourTarget {
    domain::OperationId id;
    bool outer = false;          ///< true: the raw outline (tool outside); false: a cut-out (tool inside)
    std::size_t cutoutIndex = 0; ///< cut-outs only
    /// Machine-frame XY contour, counter-clockwise (contourToMachineXY of the part-frame contour).
    geometry::Contour contour;
};

/// True when the part's outline must be routed (`!domain::isRectangularOutline(part)`).
bool needsContourRouting(const domain::Part& part) noexcept;

/// Part-frame contour (XY plane) -> machine XY, counter-clockwise. Face A up: translation by the
/// stock origin. Face B up: (x, y) -> (o.x + x, o.y + W - y) (SetupFrame::partToMachine), a
/// mirror image, so every bulge is negated and the ring is then re-oriented counter-clockwise.
geometry::Contour contourToMachineXY(const SetupFrame& frame, const geometry::Contour& partContour);

/// Targets in routing order: cut-outs first (PartShape order, while the panel is still held by its
/// outline), then the outline. Empty for parts that need no routing.
/// `model` supplies the edge bands for cutContour(); it may be null only when no outline segment
/// references an edge band (otherwise InvalidArgument: the raw outline would be wrong).
/// Errors: invalid raw outline or cut-out (validateContour), cutContour failure, id collision with
/// an operation of the part (AlreadyExists).
core::Result<std::vector<ContourTarget>> contourTargets(const domain::Part& part, const SetupFrame& frame,
                                                        const domain::Model* model);

} // namespace fos::cam
