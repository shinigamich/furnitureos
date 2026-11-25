<!-- SPDX-License-Identifier: AGPL-3.0-only -->
# hardware — generic fitting catalogue and placement rules

Module `fos::hardware` (depends on `core`, `geometry`, `domain`, `machining`).

## Honesty note

The built-in catalogue is **generic**. Dimensions are typical trade values, *not* taken from any
manufacturer's data sheet. Every entry has `dimensionsVerified = false`, an empty `manufacturer`, and a name
ending in "(generic)". **No compatibility with any real product is claimed.** Check the drilling data of the
fitting you actually buy and create your own (verified) definition for it. `checkDefinition()` emits the info
diagnostic `hardware.definition.unverified` for every unverified definition so a UI can show the flag.

The default hinge-count table is a generic rule of thumb, not a manufacturer specification.

## Catalogue (`catalog.hpp`, `builtinCatalog()`)

All parameters are millimetres in `HardwareDefinition::params`. Constants for the keys live in
`fos::hardware::keys`, for the ids in `fos::hardware::ids`.

| Id | Kind | Parameter key | Value | Meaning |
|---|---|---|---|---|
| `hw.hinge_cup_35` | Hinge | `cup_diameter` | 35 | cup hole in the door |
| | | `cup_depth` | 13 | depth of the cup hole |
| | | `cup_center_from_door_edge` | 21.5 | cup centre to the hinged door edge |
| | | `plate_line_from_front_edge` | 37 | mounting-plate hole line behind the side's front edge |
| | | `plate_hole_spacing` | 32 | distance between the two plate holes |
| | | `plate_hole_diameter` | 5 | |
| | | `plate_hole_depth` | 13 | |
| `hw.confirmat_7x50` | Confirmat | `length` | 50 | screw length |
| | | `through_diameter` | 7 | through hole in the face of the overlapping panel |
| | | `pilot_diameter` | 5 | pilot hole in the edge of the mating panel |
| | | `pilot_extra_depth` | 5 | pilot depth = `length` − face panel thickness + this (18 mm panel: 37) |
| `hw.dowel_8x30` | Dowel | `diameter` | 8 | |
| | | `length` | 30 | |
| | | `face_depth` | 12 | hole depth in the face of the overlapping panel |
| | | `edge_depth` | 20 | hole depth in the edge of the mating panel |
| `hw.shelf_pin_5` | ShelfSupport | `hole_diameter` | 5 | hole (= pin) diameter |
| | | `hole_depth` | 13 | |
| `hw.cam_15` | CamConnector | `cam_diameter` | 15 | housing hole in the panel face |
| | | `cam_depth` | 12.5 | |
| | | `cam_center_from_edge` | 34 | housing centre to the panel edge |
| | | `bolt_hole_diameter` | 8 | hole in the panel edge towards the housing |
| | | `bolt_hole_depth` | 34 | |
| | | `bolt_anchor_diameter` | 5 | hole in the face of the other panel |
| | | `bolt_anchor_depth` | 11.5 | |
| `hw.handle_bar_128` | Handle | `hole_spacing` | 128 | centre distance of the fixing holes |
| | | `hole_diameter` | 5 | through holes |
| `hw.foot_adjustable` | Foot | `height_nominal` | 100 | |
| | | `height_min` | 95 | |
| | | `height_max` | 120 | |

| `hw.slide_ball_bearing` | DrawerSlide | `undermount` | 0 | side-mounted |
| | | `length_min` / `length_max` / `length_step` | 250 / 650 / 50 | nominal lengths |
| | | `depth_allowance` | 0 | usable depth needed = L + this |
| | | `box_length_deduction` / `bottom_recess` | 0 / 0 | |
| | | `min_box_height` | 50 | |
| | | `side_clearance` | 12.7 | gap per side; box width = opening − 2 · this |
| | | `hole_line_height` | 22.5 | hole line above the box underside (cabinet and drawer rows) |
| | | `cabinet_hole_first` / `_pitch` | 37 / 128 | from the slide front, while ≤ L − `hole_end_margin` (20) |
| | | `drawer_hole_first` / `_pitch` | 32 / 128 | outer face of the box side |
| | | `cabinet_hole_diameter` / `_depth` | 5 / 12 | |
| | | `drawer_hole_diameter` / `_depth` | 2.5 / 8 | |
| `hw.slide_undermount` | DrawerSlide | `undermount` | 1 | concealed runner |
| | | `length_min` / `length_max` / `length_step` | 250 / 600 / 50 | |
| | | `depth_allowance` / `box_length_deduction` | 3 / 10 | |
| | | `bottom_recess` / `min_box_height` | 13 / 70 | box bottom raised ≥ 13 |
| | | `inner_width_deduction` / `max_side_thickness` | 42 / 16 | box inner width = opening − 42 |
| | | `hole_line_height` | 7 | cabinet hole line above the box underside |
| | | `cabinet_hole_first` / `_pitch` | 37 / 128 | |
| | | `drawer_hole_first` / `_pitch` | 20 / 224 | pilot holes in the LOWER EDGE of the box side |
| | | `cabinet_hole_diameter` / `_depth`, `drawer_hole_*` | 5 / 12, 2.5 / 10 | |

| `hw.hinge_cup_35_half` | Hinge | cup / plate keys | as `hw.hinge_cup_35` | |
| | | `crank` | 9.5 | arm crank (documentation) |
| | | `overlay_min` / `overlay_max` | 4 / 10 | door overlay on the side the hinge supports |
| `hw.hinge_cup_35_inset` | Hinge | cup / plate keys | as `hw.hinge_cup_35` | |
| | | `crank` | 16 | |
| | | `overlay_min` / `overlay_max` | −4 / 0 | negative = hinged edge inside the opening |
| `hw.lift_generic` | LiftMechanism | `side_hole_from_front` / `_spacing` | 37 / 32 | two holes in the side, along the depth |
| | | `side_hole_below_top` | 40 | below the underside of the panel above the flap |
| | | `side_hole_diameter` / `_depth` | 5 / 12 | |
| | | `flap_hole_from_side_edge` | 50 | from the flap edge at that side |
| | | `flap_hole_from_top_edge` / `_spacing` | 100 / 32 | below the flap's top edge, downwards |
| | | `flap_hole_diameter` / `_depth` | 4 / 10 | |
| | | `cabinet_height_min` / `_max` | 240 / 600 | side height the mechanism fits |
| | | `cabinet_depth_min` | 250 | side depth needed for the mechanism body |
| | | `power_factor_min` / `_max` | 400 / 1800 | flap height (mm) × weight (kg), generic convention |

`hw.sliding_generic` (kind SlidingSystem, "Sliding door system, two-lane top and bottom track (generic)",
`dimensionsVerified = false`, typical values, unverified): `top_track_height` 40, `bottom_track_height` 10,
`track_depth` 80, `track_setback` 10 (track front edge behind the carcass front), `plane_front_offset` 10
(front-lane door face behind the track front edge), `plane_pitch` 35 (back lane behind the front lane),
`door_overlap` 30, `door_height_deduction` 40, `door_bottom_clearance` 15, `door_width_min` / `_max`
400 / 1200, roller holes 4 x 10 (`roller_hole_from_side_edge` 40, `roller_hole_from_bottom_edge` 30,
`guide_hole_from_top_edge` 30), track screws 4 x 10 (`track_screw_end_distance` 50, `track_screw_pitch` 300
= largest spacing), `interior_clearance` 5, `interior_depth_min` 250. Keys: catalog.hpp.

`hw.hanger_generic` (kind Suspension, "Concealed cabinet hanger for a wall rail (generic)", typical values,
unverified): `body_height` 60 (below the underside of the panel above = reference), `body_width` 45,
`body_depth` 16 (in front of the side's rear edge), `back_space_min` 18 (rear face of the back panel at least
this far in front of the rear edge), screw pilot holes 3 x 10 at `screw_from_rear_edge` 8 and
`screw_below_top` 15 / + `screw_spacing` 32, `rail_top_below_top` 20 (rail top edge below the reference). The
generic model is surface-mounted: no pocket in the side. `hw.wall_rail_generic` (kind Other, "Steel wall rail
for cabinet hangers, per metre (generic)", `custom["unit"] = "m"`): `rail_height` 60, `rail_thickness` 12.

`hw.clothes_rail_generic` (kind Other, "Oval wardrobe rail 30x15, per metre (generic)", `custom["unit"] = "m"`,
typical values / heuristics, unverified): `rail_height` 30, `rail_thickness` 15, `hook_clearance` 35 (free space
above the rail for hanger hooks), `section_depth_min` 500 and `hanging_space_min` 800 (clear depth and clear
height below the axis for garments; GENERIC heuristics). `hw.rail_support_generic` (kind Other, "End support for
oval wardrobe rail, screw-fixed (generic)"): `body_height` 50, `body_depth` 30, two pilot holes 3 x 10
`screw_spacing` 32 apart, vertically centred on the rail axis.

**Crank model (GENERIC):** a 0 mm-crank (full-overlay, `hw.hinge_cup_35`) arm overlays about 16 mm;
each mm of crank takes 1 mm off (half 9.5 → 6.5, inset 16 → 0 = flush with the side's inner face);
`overlay_min/max` add the usual adjustment. The cup position (21.5) and plate line (37) are the same for
all three; only the reference of the plate line changes for inset doors (below).

The cam connector and the foot are **data only**: no placement rule uses them yet.

## Placement rules

All rules are pure functions: they take parts by const reference plus a config struct and return
`core::Result<Placement>` (`placement.hpp`): the `HardwareInstance` and the operations to add per part.
`applyPlacement(assembly, placement)` stores a result and replaces the previous result of the same instance.
Generated operations have `source = <instance id>` and ids `<instance id>/op.N`.

Rules never guess. If the placements of the parts do not show the required geometry, the rule returns an
error that names the parts and the numbers involved.

| Function | Header | What it does |
|---|---|---|
| `detectButtJoint`, `connectCorner` | `corner_joint.hpp` | Confirmats or dowels along a butt joint (edge of one panel on face A/B of another); coaxial face + edge holes, edge hole centred in the thickness. Optional `CornerJointConfig::zone` limits the fasteners to part of the contact (e.g. clear of a drawer-bottom groove); end offsets are measured from the ends of contact ∩ zone, an empty intersection is an error. |
| `placeShelfSupports` | `shelf_supports.hpp` | 4 pins under a horizontal shelf, on the inner faces of the two sides; pin centre = shelf underside − pin radius. |
| `placeHinges`, `hingeCount`, `hingePositions` | `hinges.hpp` | Cup hinges: cup holes in the door, mounting-plate hole pairs in the carrying panel. `HingeConfig::axis` Z = side-hung door, X = top-hinged flap (plates under the panel above; count table applied to the flap width). Overlay = side inner face − hinged edge, checked against `overlay_min/max` when the hinge has them, else 0 … side thickness + `maxSideOverhang`. **Inset rule:** when the door's inner face lies behind the side's front edge, its outer face must be 0 … `maxFrontGap` behind it (recess r) and the plate line is r + door thickness + `plate_line_from_front_edge` behind the front edge (flush 18 mm door: 55). |
| `placeLiftMechanism`, `liftPowerFactor` | `lift.hpp` | One lift mechanism per side for a top-hinged flap: 2 holes in the flap's inner face, 2 in the side's inner face (below the underside of the panel above). Errors: cabinet too low / too tall / too shallow, power factor out of range (only with `flapWeightKg` > 0), holes off panel / too deep. |
| `planSlidingDoors`, `placeSlidingTrack`, `placeSlidingDoorRollers`, `slidingTrackScrewCount` | `sliding_doors.hpp` | Inset sliding doors in a two-lane track. Plan: door width = (opening + (n − 1) · overlap) / n, height = opening − `door_height_deduction`, door i at x0 + i · (width − overlap), lanes alternate (door 1 back lane, door 2 front lane, …); **interior depth rule**: `interiorFrontY` = setback + track depth + `interior_clearance` (95 mm generic) — interior parts must start behind it, so the usable depth shrinks by it. Errors: cabinet too shallow for the tracks / for `interior_depth_min` behind them, doors too narrow (< `door_width_min` or ≤ 2 · overlap) / too wide, opening too low, door too thick for the lanes, fewer than 2 doors. Track: screw holes in the panel face looking down (top) / up (bottom), one end distance from each end, evenly spaced ≤ pitch (1764 mm → 7). Rollers: 4 holes in the door's rear face (+y). |
| `hangerBackSpace`, `placeCabinetHanger`, `placeWallRail` | `cabinet_hanger.hpp` | Wall mounting. Hanger: 2 pilot holes in the side's inner face (the face towards the panel above) at y = rear edge − 8, z = reference − 15 / − 47 (generic); errors: side not vertical facing x, too shallow / too low for the body, holes off panel / too deep, inconsistent definition (`back_space_min` < `body_depth`, screws outside the body). Rail: one instance (quantity 1, no operations) at (x0, wall, reference − `rail_top_below_top`), note "Wall rail <L> mm (cut on site) …"; error when `rail_thickness` ≥ `back_space_min`. `hangerBackSpace` gives the space the back construction must leave. |
| `planClothesRail`, `placeClothesRail` | `clothes_rail.hpp` | Wardrobe rail between two vertical panels (faces ±x). Plan: axis y = middle of the clear depth, z = section top − `heightFromTop`; errors: depth < `section_depth_min`, `heightFromTop` < max(support height / 2, rail height / 2 + `hook_clearance`), less than `hanging_space_min` below the axis ("too low"). Place: rail instance (quantity 1, `length` = clear width between the inner faces, position = left end of the axis) and support instance (quantity 2) with 2 pilot holes per panel on the inner faces at axis z ± `screw_spacing` / 2 (left upper, lower, right upper, lower); optional per-panel depth limits (shared dividers); errors: panels not vertical / not facing each other, holes off panel / too deep, equal instance ids. |
| `placeHandle` | `handle.hpp` | Two through holes for a handle on a vertical front; centred or offset from an edge; horizontal or vertical. |
| `placeDrawerSlides`, `selectSlideLength`, `drawerBoxOuterWidth`, `drawerBoxLength`, `slideNominalLengths` | `drawer_slides.hpp` | Drawer-box sizing rules and one slide's holes: cabinet row on the cabinet side's face towards the drawer, drawer row on the box side (outer face, or lower edge for undermount). Checks the side gap against the rule (± 0.5), that the slide is a nominal length and ends within the cabinet side, hole depths. Errors name parts and numbers (e.g. "The usable depth of 240 mm is too shallow for the shortest drawer slide …"). |

Compatibility checks returning diagnostics are in `compatibility.hpp` (codes `hardware.*`), the aggregated
hardware list of a model in `hardware_list.hpp`.

## Limitations

* Only axis-aligned panel arrangements are supported (parts rotated in steps of 90°).
* `connectCorner`: the whole thickness of the butting edge must lie on the face part; mitre joints,
  cam connectors and screws other than confirmats are not implemented.
* `placeHinges`: no corner or angled hinges; the cup distance is not adjusted per crank (the overlay
  range stands for the adjustment). Hinge heights are not snapped to an existing System-32 row of the side.
* `placeLiftMechanism`: drilling and range checks only; the mechanism body's space in the cabinet
  (clash with shelves) and the flap weight are not modelled.
* Sliding doors: inset two-lane system only (no overlay / face-mounted tracks, no soft-close or stopper
  positions, no door weight check); the tracks are hardware instances (length in the note), not parts.
* Cabinet hangers: drilling and the back-space value only; no load rating, no adjustment range, no
  pocket-type (recessed) hangers. The wall rail is a LINEAR article but `HardwareInstance` has no length:
  the length is in the note and the hardware list counts it as 1 piece (cost per metre is not computed).
* `placeHandle`: vertical fronts only; "left" is defined by assembly coordinates (see `handle.hpp`).
* No door weight is computed; the weight class is an input.
* Drawer slides: rows are not snapped to an existing System-32 row; no load rating / extension type; the
  undermount rear hook hole and locking devices are not modelled (only pilot holes in the side's lower edge).
