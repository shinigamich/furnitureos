// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Wall-cabinet mounting: concealed cabinet hangers (kind Suspension, keys of
/// hw.hanger_generic in catalog.hpp) hooked onto a wall rail (keys of hw.wall_rail_generic).
///
/// FRAME: the assembly frame of ARCHITECTURE.md 4.2: x to the right, y from the carcass front
/// (y = 0) into the cabinet, so the WALL is on the +y side; z up. All lengths are millimetres.
///
/// GENERIC MODEL: one hanger per cabinet side, surface-mounted on the side's inner face in its
/// top-rear corner, directly under the panel above (top panel or rear rail: its underside is
/// the REFERENCE height). The body occupies body_depth in front of the side's rear edge and
/// body_height below the reference, so the back panel must stand at least back_space_min in
/// front of the rear edge (hangerBackSpace()). The hook engages a wall rail whose top edge is
/// rail_top_below_top below the reference. These values are typical, unverified trade values.
#include "fos/hardware/compatibility.hpp"
#include "fos/hardware/placement.hpp"

namespace fos::hardware {

/// back_space_min of a hanger definition: the clear space (mm) the hanger needs between the
/// rear face of the back panel and the rear edge of the sides. InvalidArgument for a wrong
/// kind or missing / invalid parameters (also when back_space_min < body_depth).
core::Result<double> hangerBackSpace(const domain::HardwareDefinition& hanger);

struct CabinetHangerConfig {
    domain::HardwareInstanceId instanceId; ///< Required.
    /// Upper limit for the screw-hole depth (e.g. a shared panel); 0 = screw_depth unchanged.
    double maxScrewDepth = 0;
    FitOptions fit; ///< minRemainingMaterial applies to the screw holes.
};

/// ONE hanger on `side` (a vertical panel whose faces A / B point along +-x, i.e. its depth
/// runs along y) below `top` (the panel above; its underside is the reference height).
/// The inner face of the side is the one (A or B) facing the centre of `top`.
/// Produced holes (screw_diameter x screw_depth, ids `<instance>/op.1..2`, upper first) in that
/// inner face at y = side rear edge (largest y) - screw_from_rear_edge and
/// z = reference - screw_below_top, reference - screw_below_top - screw_spacing.
/// Errors (ConstraintViolation, with numbers): side not vertical facing x, reference not within
/// the side's height, side shallower than body_depth or lower than body_height below the
/// reference, holes off the panel or too deep. InvalidArgument: wrong kind, invalid parameters,
/// a screw outside the body (screw_from_rear_edge + screw_diameter / 2 > body_depth or
/// screw_below_top + screw_spacing + screw_diameter / 2 > body_height).
/// Result: one instance (quantity 1, parts {side}, position = upper screw hole, assembly frame).
core::Result<Placement> placeCabinetHanger(const domain::Part& side, const domain::Part& top,
                                           const domain::HardwareDefinition& hanger, const CabinetHangerConfig& config);

struct WallRailConfig {
    domain::HardwareInstanceId instanceId; ///< Required.
    double x0 = 0, x1 = 0;   ///< Rail ends in assembly x (length = x1 - x0, > 0).
    double wallY = 0;        ///< Wall plane (rear edge of the sides) in assembly y.
    double referenceZ = 0;   ///< Underside of the panel above the hangers (assembly z).
    std::vector<domain::PartId> parts; ///< Parts the rail carries (usually both sides).
};

/// The wall rail for a cabinet hung on `hanger`s: one instance (quantity 1, no operations) of
/// `rail` with position (x0, wallY, referenceZ - rail_top_below_top) = left end of its top edge,
/// and the note "Wall rail <length> mm (cut on site) ...". The rail is a LINEAR article (sold
/// per metre); the length is only in the note because HardwareInstance has no length field.
/// Errors: InvalidArgument for missing / invalid rail_height / rail_thickness or hanger keys;
/// ConstraintViolation when the length is not positive or rail_thickness >= back_space_min
/// (the rail would not fit into the space behind the back panel).
core::Result<Placement> placeWallRail(const domain::HardwareDefinition& rail, const domain::HardwareDefinition& hanger,
                                      const WallRailConfig& config);

} // namespace fos::hardware
