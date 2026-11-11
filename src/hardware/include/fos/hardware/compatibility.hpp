// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/core/diagnostic.hpp"
#include "fos/domain/hardware.hpp"
#include "fos/domain/part.hpp"

/// Checks whether a fitting definition suits the panels it is used on. All lengths in mm.
/// The placement rules run the relevant check themselves and refuse to place a fitting
/// that produces an error here.
///
/// Diagnostic codes (all errors unless noted; `entityId` = id of the panel concerned, or
/// the definition id for definition problems):
///   hardware.definition.missing_param     a parameter required for the kind is absent
///   hardware.definition.invalid_param     a required parameter is negative / zero / not finite
///   hardware.definition.wrong_kind        the definition is not of the kind the check/rule needs
///   hardware.definition.unverified        INFO: dimensions are generic / not checked against a data sheet
///   hardware.confirmat.too_short          screw length <= thickness of the face panel
///   hardware.confirmat.pilot_too_deep     pilot hole depth >= panel size behind the drilled edge
///   hardware.confirmat.panel_too_thin     pilot diameter + 2 * minEdgeWall > edge panel thickness
///   hardware.dowel.face_hole_too_deep     face hole leaves <= minRemainingMaterial in the face panel
///   hardware.dowel.edge_hole_too_deep     edge hole depth >= panel size behind the drilled edge
///   hardware.dowel.holes_too_shallow      face_depth + edge_depth < dowel length
///   hardware.dowel.panel_too_thin         diameter + 2 * minEdgeWall > edge panel thickness
///   hardware.hinge.cup_too_deep           cup_depth >= door thickness - minRemainingMaterial
///   hardware.hinge.plate_hole_too_deep    plate_hole_depth >= side thickness - minRemainingMaterial
///   hardware.shelf_pin.hole_too_deep      hole_depth >= side thickness - minRemainingMaterial
namespace fos::hardware {

struct FitOptions {
    /// Material that must remain (strictly more than this) under a blind face hole.
    double minRemainingMaterial = 2.0;
    /// Minimum wall on each side of a hole drilled into a panel edge.
    double minEdgeWall = 3.0;
};

/// Required parameters present and positive (see requiredParams); plus the
/// `hardware.definition.unverified` info when `dimensionsVerified` is false.
core::Diagnostics checkDefinition(const domain::HardwareDefinition& def);

/// Confirmat through the face of `facePart` into edge `edgeFace` (L1/L2/W1/W2) of `edgePart`.
core::Diagnostics checkConfirmatFit(const domain::HardwareDefinition& confirmat, const domain::Part& facePart,
                                    const domain::Part& edgePart, domain::Face edgeFace, const FitOptions& options = {});
/// Dowel between the face of `facePart` and edge `edgeFace` of `edgePart`.
core::Diagnostics checkDowelFit(const domain::HardwareDefinition& dowel, const domain::Part& facePart,
                                const domain::Part& edgePart, domain::Face edgeFace, const FitOptions& options = {});
/// Cup hinge: cup in `door`, mounting plate holes in `side`.
core::Diagnostics checkHingeFit(const domain::HardwareDefinition& hinge, const domain::Part& door,
                                const domain::Part& side, const FitOptions& options = {});
/// Shelf support pin holes in the face of `side`.
core::Diagnostics checkShelfPinFit(const domain::HardwareDefinition& pin, const domain::Part& side,
                                   const FitOptions& options = {});

} // namespace fos::hardware
