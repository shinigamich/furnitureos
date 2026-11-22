// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/hardware/compatibility.hpp"
#include "fos/hardware/placement.hpp"

namespace fos::hardware {

struct LiftConfig {
    domain::HardwareInstanceId instanceId; ///< Required.
    /// Flap weight in kg (with handle). 0 = unknown: the power factor is not checked.
    double flapWeightKg = 0;
    /// Upper limit for the side-hole depth (e.g. the shared-panel depth of a divider);
    /// 0 = use side_hole_depth unchanged.
    double maxSideHoleDepth = 0;
    FitOptions fit; ///< minRemainingMaterial applies to the side and flap holes.
};

/// Power factor of a flap: flap height (mm) x flap weight (kg). GENERIC convention used by
/// the power_factor_min / _max keys of hw.lift_generic (not a manufacturer formula).
double liftPowerFactor(double flapHeight, double flapWeightKg) noexcept;

/// ONE lift mechanism (kind LiftMechanism, keys in catalog.hpp) for a top-hinged flap on one
/// cabinet side. Assembly frame with Z up; the flap is vertical, in front of (overlay) or
/// between (inset) the sides; `side` is a vertical panel perpendicular to the flap; `top` is
/// the panel above the flap (its underside is the reference height).
/// Produced holes (ids `<instance>/op.1..4`, flap first, then side):
///  * flap, inner face (the face towards the side's centre): two holes
///    (flap_hole_diameter x flap_hole_depth) flap_hole_from_side_edge from the flap edge
///    nearest the side, at flap_hole_from_top_edge and + flap_hole_spacing below the flap's
///    top edge;
///  * side, inner face (towards the flap's centre): two holes (side_hole_diameter x depth)
///    side_hole_below_top below the underside of `top`, at side_hole_from_front and
///    + side_hole_spacing behind the side's front edge.
/// Errors (specific, with numbers): side height outside cabinet_height_min .. _max ("too
/// low" / "too tall"), side depth below cabinet_depth_min ("too shallow"), power factor
/// outside its range (only when flapWeightKg > 0), wrong kind / missing parameters, parts
/// not arranged as above, holes off a panel or too deep.
/// Result: one instance (quantity 1, parts {flap, side}, position = first side hole).
core::Result<Placement> placeLiftMechanism(const domain::Part& flap, const domain::Part& side, const domain::Part& top,
                                           const domain::HardwareDefinition& lift, const LiftConfig& config);

} // namespace fos::hardware
