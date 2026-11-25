// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Wardrobe (clothes / hanging) rails: a LINEAR rail (keys of hw.clothes_rail_generic in
/// catalog.hpp, custom["unit"] = "m") carried by two screw-fixed end supports (keys of
/// hw.rail_support_generic), one on the inner face of each panel bounding the section.
///
/// FRAME: the assembly frame of ARCHITECTURE.md 4.2: x to the right, y from the carcass front
/// (y = 0) into the cabinet, z up. All lengths are millimetres.
///
/// GENERIC MODEL: the rail axis lies in the middle of the section's clear depth and
/// `heightFromTop` below the underside of the panel above the section. Each support is centred
/// on the rail axis (body_height x body_depth) and fixed with two pilot holes screw_spacing apart,
/// vertically symmetric about the axis. The rail is cut to the clear width between the two
/// panels. The space limits (section_depth_min, hanging_space_min, hook_clearance) are typical
/// trade values / heuristics, not taken from any manufacturer's data sheet.
#include "fos/hardware/compatibility.hpp"
#include "fos/hardware/placement.hpp"

namespace fos::hardware {

/// The section the rail hangs in (assembly frame).
struct ClothesRailSpace {
    double yFront = 0, yBack = 0;  ///< Clear depth of the section (yBack > yFront).
    double zBottom = 0, zTop = 0;  ///< Clear height: top of the panel below .. underside of the panel above.
    double heightFromTop = 60;     ///< Rail axis below zTop.
};

/// Rail axis position of a valid space.
struct ClothesRailAxis {
    double y = 0; ///< (yFront + yBack) / 2
    double z = 0; ///< zTop - heightFromTop
};

/// Checks the space against the rail and support definitions and returns the rail axis.
/// InvalidArgument: a definition lacks a key / has a non-positive value, or the support's
/// screws lie outside its body (screw_spacing / 2 + screw_diameter / 2 > body_height / 2).
/// ConstraintViolation (messages name the numbers and the limit):
///  * clear depth yBack - yFront < section_depth_min ("too shallow");
///  * heightFromTop < max(body_height / 2, rail_height / 2 + hook_clearance) (support or hooks
///    would hit the panel above);
///  * axis z - zBottom < hanging_space_min ("too low" for hanging garments).
core::Result<ClothesRailAxis> planClothesRail(const domain::HardwareDefinition& rail,
                                              const domain::HardwareDefinition& support, const ClothesRailSpace& space);

struct ClothesRailConfig {
    domain::HardwareInstanceId railInstanceId;    ///< Required.
    domain::HardwareInstanceId supportInstanceId; ///< Required, different from railInstanceId.
    ClothesRailSpace space;
    /// Upper limits for the pilot-hole depth in the left / right panel (e.g. a shared divider);
    /// 0 = screw_depth unchanged.
    double maxScrewDepthLeft = 0, maxScrewDepthRight = 0;
    FitOptions fit; ///< minRemainingMaterial applies to the pilot holes.
};

/// Rail between `left` and `right`: two vertical panels whose faces point along +-x, `left` at
/// the smaller x. The inner faces are the ones facing each other.
/// Result:
///  * instance railInstanceId of `rail`: quantity 1, parts {left, right}, length = clear width
///    between the inner faces (> 0), position (left inner x, axis y, axis z), note
///    "Clothes rail <L> mm (cut to the clear width) ...";
///  * instance supportInstanceId of `support`: quantity 2, parts {left, right}, position = upper
///    hole of the left support; operations (source = supportInstanceId, ids
///    `<support>/op.1..4`): left panel upper, lower, then right panel upper, lower, each
///    screw_diameter x screw_depth on the inner face at y = axis y, z = axis z +- screw_spacing / 2.
/// Errors: those of planClothesRail; ConstraintViolation when a panel is not vertical facing x,
/// the panels do not face each other (clear width <= 0), a hole would leave the panel or be too
/// deep; InvalidArgument for missing / equal instance ids.
core::Result<Placement> placeClothesRail(const domain::Part& left, const domain::Part& right,
                                         const domain::HardwareDefinition& rail,
                                         const domain::HardwareDefinition& support, const ClothesRailConfig& config);

} // namespace fos::hardware
