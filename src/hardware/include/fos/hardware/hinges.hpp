// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/hardware/compatibility.hpp"
#include "fos/hardware/placement.hpp"

#include <vector>

namespace fos::hardware {

enum class DoorWeightClass { Light, Medium, Heavy };

/// One row of the hinge-count table: applies to doors up to `maxDoorHeight` (mm) tall.
struct HingeCountRule {
    double maxDoorHeight = 0;
    int light = 2;
    int medium = 2;
    int heavy = 3;
};

/// Default table. A GENERIC RULE OF THUMB, not a manufacturer specification — real hinge
/// counts depend on door weight, width and the hinge model; consult the hinge maker's
/// chart and replace this table through HingeConfig::rules.
///     door height   light  medium  heavy
///     <=  900 mm      2      2       3
///     <= 1600 mm      3      3       4
///     <= 2000 mm      4      4       5
///     <= 2400 mm      5      5       6
std::vector<HingeCountRule> defaultHingeCountRules();

/// Count from the first rule (in ascending maxDoorHeight order) covering `doorHeight`.
/// Error when the table is empty, no rule covers the height, or the count is < 2.
core::Result<int> hingeCount(double doorHeight, DoorWeightClass weight, const std::vector<HingeCountRule>& rules);

/// Hinge centre heights measured from the door's BOTTOM edge, ascending: the lowest at
/// `offsetFromBottom`, the highest at doorHeight - offsetFromTop, the rest evenly between.
/// Error when count < 2 or the offsets leave no room.
core::Result<std::vector<double>> hingePositions(double doorHeight, int count, double offsetFromTop,
                                                 double offsetFromBottom);

struct HingeConfig {
    domain::HardwareInstanceId instanceId; ///< Required.
    DoorWeightClass weightClass = DoorWeightClass::Medium;
    std::vector<HingeCountRule> rules = defaultHingeCountRules();
    int count = 0;                   ///< 0 = from `rules`; otherwise the exact number (>= 2).
    /// Explicit hinge centre heights measured from the door's bottom edge. When non-empty it
    /// overrides `count`, `rules` and both offsets; it must hold >= 2 strictly ascending values
    /// inside the door (0 < h < door height). Used by callers that must keep hinges clear of
    /// shelves or other fittings.
    std::vector<double> positions;
    double offsetFromTop = 100.0;    ///< Top hinge centre below the door's top edge.
    double offsetFromBottom = 100.0; ///< Bottom hinge centre above the door's bottom edge.
    /// Allowed distance between the door's inner face and the front edge of the side.
    double maxFrontGap = 5.0;
    /// The hinged door edge may project this far beyond the OUTER face of the side.
    double maxSideOverhang = 5.0;
    /// Hinge axis in the assembly frame; must be a unit assembly axis. Z (default) for a
    /// side-hung door; X for a top-hinged flap (the "side" is then the panel above the flap,
    /// "heights" / positions / offsets are measured along X from the flap's left end, and
    /// the count rule is applied to the flap width).
    geometry::Vec3 axis{0, 0, 1};
    FitOptions fit; ///< minRemainingMaterial: cup depth must be < door thickness - this (default 2 mm).
};

/// Cup hinges for a door hung on a cabinet panel (both parts in the same assembly frame,
/// hinge axis = HingeConfig::axis; Z: door on a vertical side, X: flap on the panel above).
///
/// CRANK / OVERLAY: overlay = how far the hinged door edge reaches over the side's inner
/// face (negative = the edge stays inside the opening by that gap). A hinge definition with
/// both optional keys overlay_min / overlay_max (cranked hinges: hw.hinge_cup_35_half,
/// hw.hinge_cup_35_inset) accepts exactly that overlay range; one without them (0 mm crank,
/// hw.hinge_cup_35) accepts 0 .. side thickness + maxSideOverhang (overlay doors only).
/// INSET PLATE LINE: when the door's inner face lies behind the side's front edge (inset
/// door), its outer face must be 0 .. maxFrontGap behind that edge (recess r) and the plate
/// line is measured from the door's inner face: plate line behind the side's front edge =
/// r + door thickness + plate_line_from_front_edge (flush 18 mm door: 18 + 37 = 55). For
/// overlay doors it stays plate_line_from_front_edge behind the front edge.
///
/// Derived from the placements: the door's inner face (the one facing the side), the
/// side's inner face (the one facing the door's centre), the hinged door edge (the
/// vertical door edge at the side), the side's front edge (nearest the door).
/// Produced holes, hinges numbered bottom to top, 3 operations per hinge
/// (`<instance>/op.(3i-2)` cup, `/op.(3i-1)` lower plate hole, `/op.(3i)` upper plate hole):
///  * door, inner face: cup hole (cup_diameter x cup_depth) centred
///    cup_center_from_door_edge from the hinged edge at the hinge height;
///  * side, inner face: two plate holes (plate_hole_diameter x plate_hole_depth) on the
///    line plate_line_from_front_edge behind the side's front edge, at the hinge height
///    -/+ plate_hole_spacing / 2. (The rule does not snap hinge heights to an existing
///    32 mm hole row of the side.)
///
/// Errors (nothing is guessed): door not perpendicular to the side / hinge axis not
/// parallel to the axis; overlay door not in front of the side within maxFrontGap; inset
/// door not flush / recessed within maxFrontGap; overlay outside the hinge's range (inset
/// door with a hinge without range) or door not at the side; door too narrow for the cup; cup or plate
/// holes too deep (see checkHingeFit); a plate hole would miss the side panel; no rule
/// for the door height.
/// Result: one instance (quantity = hinge count, parts {door, side}, position = lowest cup
/// centre on the door's inner face); operations for door, then side.
core::Result<Placement> placeHinges(const domain::Part& door, const domain::Part& side,
                                    const domain::HardwareDefinition& hinge, const HingeConfig& config);

} // namespace fos::hardware
