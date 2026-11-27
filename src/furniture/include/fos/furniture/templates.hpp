// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Ready-made cabinet definitions. A template is ONLY a pre-filled CabinetDefinition: every
/// parameter, option, material and section can be edited afterwards (edit.hpp).
/// Sizes are W x H x D in mm; H includes the plinth. See README.md for the full defaults.
#include "fos/furniture/definition.hpp"

namespace fos::furniture::templates {

/// Materials every template starts with.
struct TemplateMaterials {
    domain::MaterialId carcass;
    domain::MaterialId back;
    domain::MaterialId front; ///< Empty = carcass material.
    domain::EdgeBandId visibleEdge;
    std::optional<domain::EdgeBandId> hiddenEdge;
};

/// 600 x 820 x 560; plinth panel 100 (sides to the floor); stretcher rails; overlay back;
/// 1 shelf; pair of full-overlay doors, handles near the top.
CabinetDefinition baseCabinet(domain::AssemblyId id, const TemplateMaterials& materials);
/// 600 x 720 x 320; full top; overlay back; 2 shelves; one door hinged left, handle near the bottom.
CabinetDefinition wallCabinet(domain::AssemblyId id, const TemplateMaterials& materials);
/// 600 x 2100 x 560; plinth panel 100; fixed shelf above a 700 mm lower section (1 shelf),
/// upper section with 3 shelves; one full-height door hinged left.
CabinetDefinition tallCabinet(domain::AssemblyId id, const TemplateMaterials& materials);
/// 600 x 820 x 560; feet 100; full top; grooved back; stack of 4 drawer FRONTS (bottom two
/// double height). Fronts only; set FrontSpec::drawerBox for drawer boxes with slides.
CabinetDefinition drawerUnit(domain::AssemblyId id, const TemplateMaterials& materials);
/// drawerUnit() with a drawer box behind every front: FrontSpec::drawerBox = DrawerBoxSpec{}
/// (generic ball-bearing slides, grooved bottom, dowelled corners). Box material = carcass
/// material, bottom = back material (MaterialAssignment fallbacks). Name "Drawer unit with boxes".
CabinetDefinition drawerUnitWithBoxes(domain::AssemblyId id, const TemplateMaterials& materials);
/// 1000 x 2200 x 580; plinth panel 80; one partition (two equal sections: 4 shelves left,
/// 1 shelf right); pair of doors over the whole front.
CabinetDefinition wardrobeSection(domain::AssemblyId id, const TemplateMaterials& materials);
/// 800 x 1800 x 300; no plinth, no fronts; one fixed shelf in the middle, 2 adjustable
/// shelves in each half; overlay back.
CabinetDefinition openShelving(domain::AssemblyId id, const TemplateMaterials& materials);

/// Custom parameter of openShelvingRounded(): corner radius of the shelf fronts (mm).
inline constexpr const char* kShelfCornerRadius = "shelf_corner_radius";
/// openShelving() whose four adjustable shelves (shelf.1.1, shelf.1.2, shelf.2.1, shelf.2.2)
/// have rounded FRONT corners (ContourKind::RoundedCorners on the L1/W1 and L1/W2 corners) of
/// radius `shelf_corner_radius` (custom parameter, 30 mm). The front edge band runs along the
/// front straight and both arcs. Changing a shelf count leaves overrides of removed shelves in
/// place (warning furniture.contour.unused); new shelves are rectangular.
CabinetDefinition openShelvingRounded(domain::AssemblyId id, const TemplateMaterials& materials);

/// Blind corner base cabinet 1000 x 820 x 560 (README "Corner cabinets"): construction of
/// baseCabinet() (stretcher rails, plinth panel 100, overlay back, 1 shelf); CornerSpec Blind
/// on the LEFT with `blind_width` = 500 (custom parameter): blind panel 496.5 mm
/// (x 2..498.5), one door 496.5 mm (x 501.5..998) hinged right, handle near the top.
CabinetDefinition blindCornerCabinet(domain::AssemblyId id, const TemplateMaterials& materials);
/// L-shaped corner base cabinet, legs 900 x 900, depths 560 / 560, height 820 (README
/// "Corner cabinets"): plinth panels 100 (sides to the floor), full L-shaped top, overlay
/// backs on both wall sides, 1 L-shaped shelf, a Full-overlay DoorPair generated as door A
/// (315 mm) and door B (318 mm), handles near the top. Custom parameters corner_width_b = 900,
/// corner_depth_b = 560.
CabinetDefinition lCornerCabinet(domain::AssemblyId id, const TemplateMaterials& materials);

/// Tall oven housing 600 x 2100 x 560 (README "Appliance housings"): construction of
/// tallCabinet() (plinth panel 100, overlay back); horizontal split bottom -> top:
///  1. drawer section, clear height 700: DrawerStack of 2 equal fronts with drawer boxes;
///  2. open oven niche (FrontKind::ApplianceNiche, ShelfSlot ventilation), clear height
///     `niche_height` = 595 (custom parameter; GENERIC value, check the appliance's
///     installation sheet); it stands on fixed shelf `fixed_shelf.1`;
///  3. door section (the rest, 633): one door hinged left, handle near the bottom, 1 shelf.
CabinetDefinition ovenTallHousing(domain::AssemblyId id, const TemplateMaterials& materials);
/// Dishwasher front (README "Appliance housings"): an appliance gap (ApplianceGapSpec, end
/// panel on the RIGHT) of `appliance_gap_width` = 600 (custom parameter; GENERIC value, check
/// the appliance's installation sheet); width = "appliance_gap_width + thickness" (618 with
/// 18 mm boards), height 820, depth 560, plinth height 100 (SidesToFloor), no back. Decor
/// front (FrontKind::ApplianceFront, handle near the top) 716 x 595 mm, fixed to the
/// appliance door, no hinges.
CabinetDefinition dishwasherFront(domain::AssemblyId id, const TemplateMaterials& materials);

/// Panel desk 1400 x 740 x 700 (README "Desks and tables"): DeskSpec with end panels, top and
/// end panels joined by confirmats, modesty panel 350 high set back 100 from the rear edge
/// (confirmats into the ends), rounded cable slot 300 x 60 centred 20 mm from the rear edge
/// (behind the modesty panel), and a 400 mm drawer pedestal on the right with 3 drawer boxes
/// (templates::drawerUnitWithBoxes(), 400 x 722 x 582, on feet). Clear knee space 722 high,
/// 964 wide. No back panel on the desk itself (BackMount::None).
CabinetDefinition desk(domain::AssemblyId id, const TemplateMaterials& materials);
/// Table 1600 x 750 x 800: a DeskSpec on four generic legs (furniture::kGenericLegId, axes
/// 70 mm from the edges, 4 plate pilot holes each in the underside of the top), no modesty
/// panel, no cable slot, sharp corners. The project library must contain the leg definition
/// (furniture::requiredGenericHardware()).
CabinetDefinition table(domain::AssemblyId id, const TemplateMaterials& materials);

} // namespace fos::furniture::templates
