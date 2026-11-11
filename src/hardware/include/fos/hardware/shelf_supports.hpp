// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/hardware/compatibility.hpp"
#include "fos/hardware/placement.hpp"

namespace fos::hardware {

struct ShelfSupportConfig {
    domain::HardwareInstanceId instanceId; ///< Required.
    /// Pin centre to the FRONT / BACK end of the shelf edge that faces the side, mm.
    /// "Front" is the end with the smaller assembly Y (assembly front is y = 0); for a
    /// shelf edge running along assembly X it is the end with the smaller X.
    double setbackFront = 50.0;
    double setbackBack = 50.0;
    /// Largest allowed clearance between the shelf edge and the side's inner face, mm.
    double maxGap = 2.0;
    double tolerance = 0.1; ///< Allowed interference between shelf edge and side face.
    FitOptions fit;
};

/// Four support pins for a horizontal shelf lying between two vertical sides: two holes
/// in the INNER face of each side, directly under the shelf:
///     pin centre height (assembly Z) = shelf underside - hole_diameter / 2.
///
/// Geometry is taken from the placements (all three parts in the same assembly frame):
///  * the shelf must be horizontal (one of its faces A/B points straight down);
///  * for each side, the inner face is the face (A or B) that faces an edge of the shelf
///    with a clearance in [-tolerance, maxGap]; the two sides must face different edges.
/// Anything else is an error; nothing is guessed.
///
/// Result: one instance (quantity 4, parts {firstSide, secondSide, shelf}, position =
/// first hole centre on the side face, assembly frame); operations for firstSide then
/// secondSide, each front hole then back hole: ids `<instance>/op.1` .. `/op.4`.
core::Result<Placement> placeShelfSupports(const domain::Part& shelf, const domain::Part& firstSide,
                                           const domain::Part& secondSide, const domain::HardwareDefinition& pin,
                                           const ShelfSupportConfig& config);

} // namespace fos::hardware
