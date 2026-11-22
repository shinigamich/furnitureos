// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Sliding door systems (kind SlidingSystem, keys of hw.sliding_generic in catalog.hpp).
///
/// FRAME: the assembly frame of ARCHITECTURE.md 4.2: x to the right, y from the carcass
/// front (y = 0) into the cabinet, z up. All lengths are millimetres.
///
/// MODEL (generic, inset system): a top and a bottom track run between the cabinet sides,
/// screwed to the underside of the panel above the opening and to the top of the panel
/// below it, `track_setback` behind the carcass front and `track_depth` deep. Each track has
/// two lanes (planes): the FRONT plane, whose door front face is `plane_front_offset` behind
/// the track's front edge, and the BACK plane, `plane_pitch` further back. Doors alternate
/// planes: door 1 (leftmost) runs in the back plane, door 2 in the front plane, door 3 in
/// the back plane, ... Neighbouring doors overlap by `door_overlap`.
///
/// INTERIOR DEPTH RULE: everything inside the opening (partitions, fixed and adjustable
/// shelves) must stay behind the tracks: SlidingDoorPlan::interiorFrontY = track_setback +
/// track_depth + interior_clearance is the smallest y an interior part may start at, so the
/// usable interior depth shrinks from `depth` to `depth - interiorFrontY`.
#include "fos/hardware/compatibility.hpp"
#include "fos/hardware/placement.hpp"

#include <vector>

namespace fos::hardware {

/// Clear opening between the cabinet panels (assembly frame): x0..x1 between the sides,
/// z0..z1 between the panel below (bottom / fixed shelf) and the panel above (top / fixed
/// shelf); `depth` = usable depth from the carcass front (y = 0) to the back panel.
struct SlidingOpening {
    double x0 = 0, x1 = 0, z0 = 0, z1 = 0;
    double depth = 0;
};

/// One door of a plan (assembly frame). plane 0 = front track lane, 1 = back track lane.
struct SlidingDoorSlot {
    double x0 = 0, x1 = 0;    ///< Left / right door edge.
    double z0 = 0, z1 = 0;    ///< Bottom / top door edge.
    double yFront = 0, yBack = 0; ///< Visible front face / rear face.
    int plane = 0;
};

/// Result of planSlidingDoors().
struct SlidingDoorPlan {
    double doorWidth = 0;   ///< (opening width + (n - 1) * door_overlap) / n
    double doorHeight = 0;  ///< opening height - door_height_deduction
    double overlap = 0;     ///< door_overlap
    double trackX0 = 0, trackX1 = 0; ///< Track ends (= opening width; track length = x1 - x0).
    double trackY0 = 0, trackY1 = 0; ///< Track front / rear edge (track_setback .. + track_depth).
    double interiorFrontY = 0;       ///< INTERIOR DEPTH RULE above.
    std::vector<SlidingDoorSlot> doors; ///< Left to right.
};

/// Sizes and positions `doorCount` doors of thickness `doorThickness` in `opening`.
/// Door i (0-based, left to right): x0 = opening.x0 + i * (doorWidth - overlap); plane =
/// (i even ? back : front); z0 = opening.z0 + door_bottom_clearance.
/// Errors (ConstraintViolation, with numbers and the system name): fewer than 2 doors;
/// cabinet too shallow for the tracks (depth < interiorFrontY) or leaving less than
/// interior_depth_min behind them; doors narrower than door_width_min (or not wider than
/// 2 * door_overlap, so doors in the same plane would touch) or wider than door_width_max;
/// opening not higher than door_height_deduction; door too thick for the lanes
/// (doorThickness >= plane_pitch, or plane_front_offset + plane_pitch + doorThickness >
/// track_depth). InvalidArgument: wrong kind, missing / invalid parameters,
/// door_bottom_clearance >= door_height_deduction.
core::Result<SlidingDoorPlan> planSlidingDoors(const domain::HardwareDefinition& system, const SlidingOpening& opening,
                                               int doorCount, double doorThickness);

/// Number of track screw holes for a track of `length` mm: one track_screw_end_distance (e)
/// from each end and evenly spaced in between at most track_screw_pitch (p) apart:
/// ceil((length - 2e) / p) + 1 (e.g. 1764 mm, e 50, p 300: ceil(5.547) + 1 = 7). 0 when
/// length < 2e.
int slidingTrackScrewCount(double length, double endDistance, double pitch) noexcept;

struct SlidingTrackConfig {
    domain::HardwareInstanceId instanceId; ///< Required.
    bool top = true;        ///< true: top track under `panel`; false: bottom track on it.
    double x0 = 0, x1 = 0;  ///< Track ends in assembly x (SlidingDoorPlan::trackX0 / X1).
    double y = 0;           ///< Screw line in assembly y (track centre line).
    double maxHoleDepth = 0; ///< Upper limit for the screw-hole depth (shared panel); 0 = none.
    FitOptions fit;          ///< minRemainingMaterial applies to the screw holes.
};

/// One track (quantity 1, parts {panel}, position = first screw hole, note with the length)
/// and its screw holes (track_screw_diameter x track_screw_depth, ids `<instance>/op.1..N`,
/// left to right) in the face of the horizontal `panel` that looks down (top track) or up
/// (bottom track). Errors: panel not horizontal, track shorter than two end distances,
/// holes off the panel or too deep.
core::Result<Placement> placeSlidingTrack(const domain::Part& panel, const domain::HardwareDefinition& system,
                                          const SlidingTrackConfig& config);

struct SlidingRollerConfig {
    domain::HardwareInstanceId instanceId; ///< Required.
    FitOptions fit;
};

/// Roller and top-guide fixing holes of ONE door (quantity 1 = one roller/guide set; parts
/// {door}). The door is vertical in an x/z plane; holes go into its REAR face (outward
/// normal +y): op.1 / op.2 bottom rollers roller_hole_from_bottom_edge above the bottom
/// edge, op.3 / op.4 top guides guide_hole_from_top_edge below the top edge; each pair
/// roller_hole_from_side_edge in from the left / right edge (left first).
/// Errors: door not vertical facing y, holes off the door or too deep.
core::Result<Placement> placeSlidingDoorRollers(const domain::Part& door, const domain::HardwareDefinition& system,
                                                const SlidingRollerConfig& config);

} // namespace fos::hardware
