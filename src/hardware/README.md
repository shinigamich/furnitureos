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
| `detectButtJoint`, `connectCorner` | `corner_joint.hpp` | Confirmats or dowels along a butt joint (edge of one panel on face A/B of another); coaxial face + edge holes, edge hole centred in the thickness. |
| `placeShelfSupports` | `shelf_supports.hpp` | 4 pins under a horizontal shelf, on the inner faces of the two sides; pin centre = shelf underside − pin radius. |
| `placeHinges`, `hingeCount`, `hingePositions` | `hinges.hpp` | Cup hinges for an overlay door on a vertical side: cup holes in the door, mounting-plate hole pairs in the side. |
| `placeHandle` | `handle.hpp` | Two through holes for a handle on a vertical front; centred or offset from an edge; horizontal or vertical. |
| `placeDrawerSlides`, `selectSlideLength`, `drawerBoxOuterWidth`, `drawerBoxLength`, `slideNominalLengths` | `drawer_slides.hpp` | Drawer-box sizing rules and one slide's holes: cabinet row on the cabinet side's face towards the drawer, drawer row on the box side (outer face, or lower edge for undermount). Checks the side gap against the rule (± 0.5), that the slide is a nominal length and ends within the cabinet side, hole depths. Errors name parts and numbers (e.g. "The usable depth of 240 mm is too shallow for the shortest drawer slide …"). |

Compatibility checks returning diagnostics are in `compatibility.hpp` (codes `hardware.*`), the aggregated
hardware list of a model in `hardware_list.hpp`.

## Limitations

* Only axis-aligned panel arrangements are supported (parts rotated in steps of 90°).
* `connectCorner`: the whole thickness of the butting edge must lie on the face part; mitre joints,
  cam connectors and screws other than confirmats are not implemented.
* `placeHinges`: overlay doors on a vertical side only (no inset doors, no flap doors, no corner or
  angled hinges). Hinge heights are not snapped to an existing System-32 row of the side.
* `placeHandle`: vertical fronts only; "left" is defined by assembly coordinates (see `handle.hpp`).
* No door weight is computed; the weight class is an input.
* Drawer slides: rows are not snapped to an existing System-32 row; no load rating / extension type; the
  undermount rear hook hole and locking devices are not modelled (only pilot holes in the side's lower edge).
