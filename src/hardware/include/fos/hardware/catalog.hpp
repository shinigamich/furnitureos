// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/core/result.hpp"
#include "fos/domain/hardware.hpp"

#include <vector>

/// Built-in GENERIC fitting catalogue.
///
/// HONESTY NOTE: every definition returned here describes a generic, typical fitting.
/// The dimensions are common trade values, NOT taken from any manufacturer's data sheet:
/// `dimensionsVerified` is false, `manufacturer` is empty and every name ends in
/// "(generic)". No compatibility with any real product is claimed. Always check the
/// drilling data of the fitting you actually buy.
///
/// PARAMETER KEYS (HardwareDefinition::params, all in mm) by definition:
///
///  hw.hinge_cup_35      kind Hinge        "Cup hinge 35 mm with mounting plate (generic)"
///    cup_diameter               35    diameter of the cup hole in the door
///    cup_depth                  13    depth of the cup hole
///    cup_center_from_door_edge  21.5  cup centre to the hinged door edge
///    plate_line_from_front_edge 37    mounting-plate hole line to the front edge of the side
///    plate_hole_spacing         32    distance between the two plate holes (along the height)
///    plate_hole_diameter        5
///    plate_hole_depth           13
///
///  hw.confirmat_7x50    kind Confirmat    "Confirmat screw 7x50 (generic)"
///    length                     50    screw length under the head surface
///    through_diameter           7     through hole in the face of the overlapping panel
///    pilot_diameter             5     pilot hole in the edge of the mating panel
///    pilot_extra_depth          5     pilot depth = length - face panel thickness + this
///                                     (18 mm panel: 50 - 18 + 5 = 37)
///
///  hw.dowel_8x30        kind Dowel        "Wooden dowel 8x30 (generic)"
///    diameter                   8
///    length                     30
///    face_depth                 12    hole depth in the face of the overlapping panel
///    edge_depth                 20    hole depth in the edge of the mating panel
///
///  hw.shelf_pin_5       kind ShelfSupport "Shelf support pin 5 mm (generic)"
///    hole_diameter              5     (also the pin diameter)
///    hole_depth                 13
///
///  hw.cam_15            kind CamConnector "Cam connector 15 mm with 8 mm bolt hole (generic)"
///    cam_diameter               15    housing hole in the face of the panel carrying the cam
///    cam_depth                  12.5  housing depth
///    cam_center_from_edge       34    housing centre to the panel edge
///    bolt_hole_diameter         8     hole in the edge of the same panel towards the housing
///    bolt_hole_depth            34
///    bolt_anchor_diameter       5     hole in the face of the other panel for the bolt thread
///    bolt_anchor_depth          11.5
///    (Typical values for this family of connectors; unverified. Data only: no placement
///     rule for cam connectors is implemented.)
///
///  hw.handle_bar_128    kind Handle       "Bar handle, 128 mm hole spacing (generic)"
///    hole_spacing               128   centre distance of the two fixing holes
///    hole_diameter              5     through holes
///
///  hw.foot_adjustable   kind Foot         "Adjustable cabinet foot 100 mm (generic)"
///    height_nominal             100
///    height_min                 95
///    height_max                 120
///    (Screw-on fitting; no drilling parameters, data only.)
///
///  hw.slide_ball_bearing  kind DrawerSlide "Ball-bearing drawer slide, side-mounted (generic)"
///  hw.slide_undermount    kind DrawerSlide "Concealed undermount runner (generic)"
///    (rules and meaning: drawer_slides.hpp)          ball-bearing  undermount
///    undermount                 0 = side-mounted, 1 = undermount   0          1
///    length_min / length_max    nominal lengths                    250 / 650  250 / 600
///    length_step                                                   50         50
///    depth_allowance            usable depth needed = L + this     0          3
///    box_length_deduction       box length = L - this              0          10
///    bottom_recess              min. height of the box bottom      0          13
///                               above the box underside
///    min_box_height             lowest drawer box (side height)    50         70
///    side_clearance             gap per side (side-mounted)        12.7       -
///    inner_width_deduction      box inner width = opening - this   -          42
///    max_side_thickness         thickest box side (undermount)     -          16
///    hole_line_height           hole line above box underside      22.5       7
///    cabinet_hole_first / _pitch  along the slide from its front   37 / 128   37 / 128
///    drawer_hole_first / _pitch                                    32 / 128   20 / 224
///    hole_end_margin            last hole to the slide's rear end  20         20
///    cabinet_hole_diameter / _depth  (system / euro-screw holes)   5 / 12     5 / 12
///    drawer_hole_diameter / _depth   (screw pilot holes)           2.5 / 8    2.5 / 10
///    (Typical values; unverified. Undermount drawer holes are in the LOWER EDGE of the box
///     side, ball-bearing ones in its outer face.)
///
///  hw.hinge_cup_35_half   kind Hinge  "Cup hinge 35 mm, half overlay, 9.5 mm crank (generic)"
///  hw.hinge_cup_35_inset  kind Hinge  "Cup hinge 35 mm, inset, 16 mm crank (generic)"
///    cup / plate drilling as hw.hinge_cup_35 (35 x 13 cup 21.5 from the edge, plate line 37,
///    2 x 5 x 13 plate holes 32 apart), plus (OPTIONAL keys for any Hinge, see hinges.hpp):
///                                                    half    inset
///    crank          arm crank (documentation)        9.5     16
///    overlay_min    supported door overlay on the    4       -4
///    overlay_max    side, mm (negative = inset gap)  10      0
///    GENERIC model: a 0 mm-crank (full overlay) arm overlays about 16 mm; each mm of crank
///    takes 1 mm off (half: 6.5, inset: 0 = door edge flush with the side's inner face); the
///    range adds the usual adjustment. hw.hinge_cup_35 is the 0 mm-crank variant (no range
///    keys: legacy overlay check). Inset plate line: see placeHinges.
///
///  hw.lift_generic   kind LiftMechanism  "Lift mechanism for top-hinged flaps (generic)"
///    side_hole_from_front       37    first hole in the cabinet side behind its front edge
///    side_hole_spacing          32    second hole this far further back (same height)
///    side_hole_below_top        40    hole line below the underside of the panel above the flap
///    side_hole_diameter / _depth  5 / 12
///    flap_hole_from_side_edge   50    flap holes from the flap's edge at that side, inward
///    flap_hole_from_top_edge    100   first flap hole below the flap's top edge
///    flap_hole_spacing          32    second flap hole this far further down
///    flap_hole_diameter / _depth  4 / 10
///    cabinet_height_min / _max  240 / 600  height of the cabinet side the mechanism fits
///    cabinet_depth_min          250   depth of the cabinet side needed for the mechanism body
///    power_factor_min / _max    400 / 1800  flap height (mm) x flap weight (kg) range
///    (Typical values for a small lift stay; unverified. One mechanism per side.)
///
///  hw.sliding_generic  kind SlidingSystem  "Sliding door system, two-lane top and bottom track (generic)"
///    (rules and frame: sliding_doors.hpp)
///    top_track_height           40    top track profile height (under the panel above)
///    bottom_track_height        10    bottom track profile height (on the panel below)
///    track_depth                80    depth of both profiles (two lanes)
///    track_setback              10    front edge of the tracks behind the carcass front
///    plane_front_offset         10    front-lane door front face behind the track front edge
///    plane_pitch                35    back-lane door front face this far behind the front lane's
///    door_overlap               30    overlap of neighbouring doors
///    door_height_deduction      40    door height = opening height - this (track clearance)
///    door_bottom_clearance      15    door bottom edge above the opening bottom (25 remain on top)
///    door_width_min / _max      400 / 1200
///    roller_hole_from_side_edge   40  roller / guide holes in from the left and right door edge
///    roller_hole_from_bottom_edge 30  bottom roller holes above the door bottom edge
///    guide_hole_from_top_edge     30  top guide holes below the door top edge
///    roller_hole_diameter / _depth  4 / 10   (rear face of the door)
///    track_screw_end_distance   50    first / last track screw from the track ends
///    track_screw_pitch          300   largest spacing of the track screws
///    track_screw_diameter / _depth  4 / 10   (pilot holes in the top / bottom panel)
///    interior_clearance         5     gap between the track rear edge and the interior
///    interior_depth_min         250   interior depth that must remain behind the tracks
///    (Typical values for a light wardrobe system; unverified. Data for an inset system.)
namespace fos::hardware {

namespace keys {
inline constexpr const char* kCupDiameter = "cup_diameter";
inline constexpr const char* kCupDepth = "cup_depth";
inline constexpr const char* kCupCenterFromDoorEdge = "cup_center_from_door_edge";
inline constexpr const char* kPlateLineFromFrontEdge = "plate_line_from_front_edge";
inline constexpr const char* kPlateHoleSpacing = "plate_hole_spacing";
inline constexpr const char* kPlateHoleDiameter = "plate_hole_diameter";
inline constexpr const char* kPlateHoleDepth = "plate_hole_depth";
inline constexpr const char* kLength = "length";
inline constexpr const char* kThroughDiameter = "through_diameter";
inline constexpr const char* kPilotDiameter = "pilot_diameter";
inline constexpr const char* kPilotExtraDepth = "pilot_extra_depth";
inline constexpr const char* kDiameter = "diameter";
inline constexpr const char* kFaceDepth = "face_depth";
inline constexpr const char* kEdgeDepth = "edge_depth";
inline constexpr const char* kHoleDiameter = "hole_diameter";
inline constexpr const char* kHoleDepth = "hole_depth";
inline constexpr const char* kHoleSpacing = "hole_spacing";
inline constexpr const char* kCamDiameter = "cam_diameter";
inline constexpr const char* kCamDepth = "cam_depth";
inline constexpr const char* kCamCenterFromEdge = "cam_center_from_edge";
inline constexpr const char* kBoltHoleDiameter = "bolt_hole_diameter";
inline constexpr const char* kBoltHoleDepth = "bolt_hole_depth";
inline constexpr const char* kBoltAnchorDiameter = "bolt_anchor_diameter";
inline constexpr const char* kBoltAnchorDepth = "bolt_anchor_depth";
inline constexpr const char* kHeightNominal = "height_nominal";
inline constexpr const char* kHeightMin = "height_min";
inline constexpr const char* kHeightMax = "height_max";
inline constexpr const char* kUndermount = "undermount";
inline constexpr const char* kLengthMin = "length_min";
inline constexpr const char* kLengthMax = "length_max";
inline constexpr const char* kLengthStep = "length_step";
inline constexpr const char* kDepthAllowance = "depth_allowance";
inline constexpr const char* kBoxLengthDeduction = "box_length_deduction";
inline constexpr const char* kBottomRecess = "bottom_recess";
inline constexpr const char* kMinBoxHeight = "min_box_height";
inline constexpr const char* kSideClearance = "side_clearance";
inline constexpr const char* kInnerWidthDeduction = "inner_width_deduction";
inline constexpr const char* kMaxSideThickness = "max_side_thickness";
inline constexpr const char* kHoleLineHeight = "hole_line_height";
inline constexpr const char* kCabinetHoleFirst = "cabinet_hole_first";
inline constexpr const char* kCabinetHolePitch = "cabinet_hole_pitch";
inline constexpr const char* kDrawerHoleFirst = "drawer_hole_first";
inline constexpr const char* kDrawerHolePitch = "drawer_hole_pitch";
inline constexpr const char* kHoleEndMargin = "hole_end_margin";
inline constexpr const char* kCabinetHoleDiameter = "cabinet_hole_diameter";
inline constexpr const char* kCabinetHoleDepth = "cabinet_hole_depth";
inline constexpr const char* kDrawerHoleDiameter = "drawer_hole_diameter";
inline constexpr const char* kDrawerHoleDepth = "drawer_hole_depth";
inline constexpr const char* kCrank = "crank";
inline constexpr const char* kOverlayMin = "overlay_min";
inline constexpr const char* kOverlayMax = "overlay_max";
inline constexpr const char* kSideHoleFromFront = "side_hole_from_front";
inline constexpr const char* kSideHoleSpacing = "side_hole_spacing";
inline constexpr const char* kSideHoleBelowTop = "side_hole_below_top";
inline constexpr const char* kSideHoleDiameter = "side_hole_diameter";
inline constexpr const char* kSideHoleDepth = "side_hole_depth";
inline constexpr const char* kFlapHoleFromSideEdge = "flap_hole_from_side_edge";
inline constexpr const char* kFlapHoleFromTopEdge = "flap_hole_from_top_edge";
inline constexpr const char* kFlapHoleSpacing = "flap_hole_spacing";
inline constexpr const char* kFlapHoleDiameter = "flap_hole_diameter";
inline constexpr const char* kFlapHoleDepth = "flap_hole_depth";
inline constexpr const char* kCabinetHeightMin = "cabinet_height_min";
inline constexpr const char* kCabinetHeightMax = "cabinet_height_max";
inline constexpr const char* kCabinetDepthMin = "cabinet_depth_min";
inline constexpr const char* kPowerFactorMin = "power_factor_min";
inline constexpr const char* kPowerFactorMax = "power_factor_max";
inline constexpr const char* kTopTrackHeight = "top_track_height";
inline constexpr const char* kBottomTrackHeight = "bottom_track_height";
inline constexpr const char* kTrackDepth = "track_depth";
inline constexpr const char* kTrackSetback = "track_setback";
inline constexpr const char* kPlaneFrontOffset = "plane_front_offset";
inline constexpr const char* kPlanePitch = "plane_pitch";
inline constexpr const char* kDoorOverlap = "door_overlap";
inline constexpr const char* kDoorHeightDeduction = "door_height_deduction";
inline constexpr const char* kDoorBottomClearance = "door_bottom_clearance";
inline constexpr const char* kDoorWidthMin = "door_width_min";
inline constexpr const char* kDoorWidthMax = "door_width_max";
inline constexpr const char* kRollerHoleFromSideEdge = "roller_hole_from_side_edge";
inline constexpr const char* kRollerHoleFromBottomEdge = "roller_hole_from_bottom_edge";
inline constexpr const char* kGuideHoleFromTopEdge = "guide_hole_from_top_edge";
inline constexpr const char* kRollerHoleDiameter = "roller_hole_diameter";
inline constexpr const char* kRollerHoleDepth = "roller_hole_depth";
inline constexpr const char* kTrackScrewEndDistance = "track_screw_end_distance";
inline constexpr const char* kTrackScrewPitch = "track_screw_pitch";
inline constexpr const char* kTrackScrewDiameter = "track_screw_diameter";
inline constexpr const char* kTrackScrewDepth = "track_screw_depth";
inline constexpr const char* kInteriorClearance = "interior_clearance";
inline constexpr const char* kInteriorDepthMin = "interior_depth_min";
} // namespace keys

namespace ids {
inline constexpr const char* kHingeCup35 = "hw.hinge_cup_35";
inline constexpr const char* kConfirmat7x50 = "hw.confirmat_7x50";
inline constexpr const char* kDowel8x30 = "hw.dowel_8x30";
inline constexpr const char* kShelfPin5 = "hw.shelf_pin_5";
inline constexpr const char* kCam15 = "hw.cam_15";
inline constexpr const char* kHandleBar128 = "hw.handle_bar_128";
inline constexpr const char* kFootAdjustable = "hw.foot_adjustable";
inline constexpr const char* kSlideBallBearing = "hw.slide_ball_bearing";
inline constexpr const char* kSlideUndermount = "hw.slide_undermount";
inline constexpr const char* kHingeCup35Half = "hw.hinge_cup_35_half";
inline constexpr const char* kHingeCup35Inset = "hw.hinge_cup_35_inset";
inline constexpr const char* kLiftGeneric = "hw.lift_generic";
inline constexpr const char* kSlidingGeneric = "hw.sliding_generic";
} // namespace ids

/// The generic catalogue, in the fixed order of the list above. unitCost is 0 (unknown).
std::vector<domain::HardwareDefinition> builtinCatalog();

/// Entry of builtinCatalog() by id; NotFound error naming the id otherwise.
core::Result<domain::HardwareDefinition> builtinDefinition(const std::string& id);

/// Parameter keys a definition of the given kind must provide for the placement rules of
/// this module (empty for kinds without a rule).
std::vector<std::string> requiredParams(domain::HardwareKind kind);

/// Value of a parameter; InvalidArgument error naming the definition and the key when it
/// is missing, not finite, negative, or zero while `allowZero` is false.
core::Result<double> requireParam(const domain::HardwareDefinition& def, const std::string& key,
                                  bool allowZero = false);

} // namespace fos::hardware
