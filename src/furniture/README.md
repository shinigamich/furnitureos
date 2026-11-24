<!-- SPDX-License-Identifier: AGPL-3.0-only -->
# furniture — parametric cabinet generator

Module `fos::furniture` (depends on `core`, `geometry`, `parametric`, `domain`, `machining`, `hardware`).

A `CabinetDefinition` (definition.hpp) is plain, copyable data: a parameter graph, construction
options, materials, hardware selection and an interior section tree. `generate()` (generate.hpp)
derives a `domain::Assembly` from it — pure and deterministic. Edits go through edit.hpp (atomic:
on error the definition is unchanged). Templates (templates.hpp) are only pre-filled definitions.
All lengths are millimetres; the assembly frame follows ARCHITECTURE.md 4.2 with the carcass front
at y = 0, x to the right, z up.

## Parameters

`standardParameters(options)` (parameters.hpp) is the executable list of every standard name with
its default / formula; inputs are constants, derived parameters are expressions chosen by the
construction options (e.g. `back_width` = `width - 2 * back_inset` for an Overlay back,
`inner_width + 2 * back_engagement` for a Grooved one). Setting a derived or material-synchronised
parameter PINS it (`pinnedParameters`); pinned parameters are not rewritten by option changes.

* When an edit changes the construction options and the standard formula of a pinned derived
  parameter differs under the new options, the edit result carries the warning
  `furniture.parameter.pinned_formula_changed` naming the parameter, both formulas, the pinned and
  the standard value. The pinned value is kept; `resetParameter()` restores the standard formula.

## Slots (part ids are `<cabinet id>/<slot>`)

`side.left`, `side.right`, `bottom`, `top` or `rail.front` + `rail.back`, `plinth`, `back`;
`partition.<path>.<k>` and `fixed_shelf.<path>.<k>` (k-th divider of the section at `<path>`),
`shelf.<path>.<i>` (adjustable shelves, bottom to top), `door.<path>` / `door.<path>.left|right`,
`sliding.<path>.<n>` (sliding doors, left to right), `drawer.<path>.<i>` (bottom to top) and, with a drawer box, `drawer.<path>.<i>.side.left|side.right|box_front|box_back|bottom`.
Section paths: root `""`, children `1`, `2`, `2.1`, ... Empty
path segments are skipped (`shelf.1` for the root's first shelf). Ids depend only on the tree
position, so changing a shelf count only adds/removes the trailing ids of that section.
Hardware instances: `hw.joint.<edge slot>.<face slot>`, `hw.pins.<shelf slot>`,
`hw.hinges.<door slot>`, `hw.handle.<front slot>`, `hw.slide.<drawer slot>.left|right`,
`hw.joint.<drawer slot>.box_front|box_back.left|right` (drawer-box corners), `hw.track.<path>.top|bottom`,
`hw.rollers.<sliding slot>`, `hw.feet`; recessed grips of sliding doors have source `<cabinet>/rule.sliding_grip`; back grooves
have source `<cabinet>/rule.back_groove`, drawer-bottom grooves `<cabinet>/rule.drawer_bottom_groove`.

## Interior

Split sizes: `Fixed` (mm), `Expression` (mm over the graph) and `Weight` (share of the rest).
Errors name the section and the numbers (sum of fixed sizes, available space, excess / unassigned
amount). Adjustable shelves: n shelves leave n + 1 equal clear spaces; each shelf is
`shelf_clearance` shorter than the opening at both ends and set back `shelf_setback`.

### Shared panels (interior dividers)

A partition or fixed shelf can be drilled at the same spot from both faces. Rule (constant
`kSharedPanelWeb` = 2 mm in `gen_context.hpp`): every blind hole drilled into a FACE of a divider is
at most `(thickness - 2) / 2` deep (8 mm in 18 mm board, 7 mm in 16 mm), so two opposite holes keep
2 mm of material between them. It applies per panel:

* shelf pins: only the support that is a divider is limited; an outer side keeps the catalogue depth
  (e.g. 13 mm);
* hinge mounting plates on a partition;
* divider joints whose FACE part is a divider (fixed shelf against a partition, partition on a fixed
  shelf) are always blind DOWEL joints (`HardwareSelection::dowel`), whatever `dividerFastener`
  says (except `None`): a confirmat needs a through hole, which would meet the joint drilled from the
  other face (fixed shelves at the same height on both sides of a partition). The dowel face hole is
  limited as above and the edge hole is deepened to `length - face depth` (8 + 22 for 8x30) so the
  dowel is fully seated. Joints into OUTER panels keep the selected fastener.

### Shelf-pin gap

The generator places a shelf exactly `shelf_clearance` from its supports and hands that geometry to
`hardware::placeShelfSupports` with `maxGap = shelf_clearance + kShelfPinGapSlack` (0.5 mm, only a
rounding allowance).

### Shelf pins and joints

Hardware is placed in generation order: carcass joints, then per section its front, its dividers with their
joints, then the child sections (shelves with their pins). **Joints take precedence over shelf pins**: when a pin
hole of a shelf would intersect a hole already drilled into the same panel (`machining::holesCollide`; typically
the confirmat or dowel edge hole at a partition end in a low cabinet, e.g. the wardrobe template at 320 or 337 mm
height), the pins of that shelf are not drilled at all (both sides) and the warning `furniture.shelf_pins.dropped`
names the shelf, the pin hole and the hole it would meet. The shelf is still generated (unsupported); the remedy is
fewer shelves, a taller section or another fastener. A height sweep of every template (200 … 3000 mm) has no
machining collision.

## Back panel and grooves

Overlay (on the rear, inset `back_inset`), Grooved (in grooves `groove_depth` deep, `groove_distance`
in front of the rear edge; the back enters each groove by `back_engagement = groove_depth -
groove_clearance`), Inset (between the carcass panels).

Groove end rule: a groove runs THROUGH an end of its part only where that end is hidden by a mating
panel; at an exposed or edge-banded end it stops `groove_clearance` beyond the back panel (the back's
extent plus the clearance it also has at the groove bottom — exactly in line with the bottoms of the
crossing grooves). Sides: the lower end is hidden only with an OverSides bottom, the upper end only
with an OverSides top. Bottom / top: ends hidden with BetweenSides (they butt the sides), stopped with
OverSides. The rear rail always sits between the sides (through).

## Fronts

Full overlay covers an outer panel by `overlay_outer = thickness - front_reveal` and a shared panel by
`overlay_shared = thickness / 2 - front_gap / 2`; Half uses `overlay_shared` everywhere; Inset stays
`inset_gap` inside the opening (y 0..front_thickness). A door pair splits the front width minus
`front_gap` equally. Drawer stacks distribute the front height minus the gaps like section sizes.

**Front face rule: for every front (door or drawer front) face A is the inner (cabinet-side) face and
face B the visible outer face.** Doors: length vertical, width along x. Drawer fronts: length along x
(grain horizontal), width from the top edge downwards (L1 = top edge). Handle holes are THROUGH holes
recorded on face A (the same (u, v) on face B is where they leave the visible face); hinge cups are
drilled into face A.

### Hinge crank by overlay

Full → `HardwareSelection::hinge` (0 mm crank), Half → `hingeHalf` (`hw.hinge_cup_35_half`, 9.5 mm),
Inset → `hingeInset` (`hw.hinge_cup_35_inset`, 16 mm); see hardware README for the overlay ranges and the
inset plate line (flush 18 mm inset door: plates 18 + 37 = 55 mm behind the side's front edge). A full-overlay
door on a shared panel keeps the full-overlay hinge (legacy check). If the half / inset hinge is not in the
hardware library (old project libraries): Half falls back to `hinge` with warning
`furniture.front.half_overlay_hinge`, Inset gets no hinges with warning `furniture.front.inset_hinges`.

### Flap (`FrontKind::Flap`)

One front (slot `flap.<path>`, role Door, same size rule as a single door) hinged at its TOP edge to the
panel above the section (carcass top or a fixed shelf). With `hinges = true`: cup hinges `hw.hinges.<slot>`
(hinge axis X, crank by overlay, count from the table applied to the flap width, `hinge_offset` from each
end; cups on the flap's inner face, plates under the panel above) and one lift mechanism per side
`hw.lift.<slot>.left|right` (`HardwareSelection::lift`, default `hw.lift_generic`). A section closed by
stretcher rails cannot take a flap (generation error). Lift range errors (cabinet too low / tall / shallow)
are `furniture.hardware.placement_failed`. The handle is horizontal. `hinges = false` = front only.
Project files: `kind: "Flap"`; `hardware.hingeHalf|hingeInset|lift` written only when not default.

### Sliding doors (`FrontKind::SlidingDoors`)

Inset doors running in the two-lane track system `HardwareSelection::slidingSystem` (default
`hw.sliding_generic`; rules `hardware/sliding_doors.hpp`), usually on the root section. `FrontSpec::sliding`:
`count` (2..6) or `countExpression` (graph expression giving a whole number 2..6, e.g.
`if(width > 2000, 3, 2)`). `overlay`, `hingeSide` and `hinges` are ignored. Parts `sliding.<path>.<n>` (role
Door, front material, face B visible at the lane's front face): width = (opening + (n − 1) · overlap) / n,
height = opening − deduction, odd doors in the back lane, even doors in the front lane (part custom
`furniture.sliding_plane` = `back` / `front`). Example 1800 × 2400 × 600 wardrobe (opening 1764 × 2364):
2 doors 897 × 2324 (x 18..915 back lane y 55..73, x 885..1782 front lane y 20..38), 3 doors 608 wide at
x 18 / 596 / 1174. Tracks `hw.track.<path>.top|bottom` (length = opening width, also in `HardwareInstance::length`) with their screw holes in
the panel above (must be a full panel: stretcher rails fail generation) and below (shared-panel depth limit
on a fixed shelf); `hw.rollers.<slot>` = 4 roller / guide holes on each door's rear face. Handle: `None` = no
grip; otherwise one recessed grip (Pocket on face B, centred on the door width; Center = mid height, Top /
Bottom = centre `handle_end_offset` from that edge; optional parameters `sliding_grip_length` 200,
`sliding_grip_width` 40, `sliding_grip_depth` 8).
**Interior depth rule:** partitions, fixed shelves and adjustable shelves of the section and all its
descendants start at y = `interiorFrontY` = track setback + track depth + interior clearance (95 mm generic)
and end at the back, i.e. their depth is `interior_depth − 95` (shelves: min(`shelf_depth`, that)); the
standard parameters are not changed. Errors (generation fails, edits are refused): cabinet too shallow for the
tracks (e.g. depth 300: only 205 mm remain behind them, 250 needed), doors too narrow / wide, count outside
2..6 or a non-integer expression, missing system definition. Project files: `kind: "SlidingDoors"`, front key
`sliding: {count, countExpression?}` and `hardware.slidingSystem` written only when used / not default.

### Wall mounting (`HardwareSelection::hanger`)

`hanger` empty (default, also for every older file) = no wall-mounting hardware. Set (e.g.
`hw.hanger_generic`, rules `hardware/cabinet_hanger.hpp`): hangers `hw.hanger.left|right` with two pilot holes
each in the top-rear corner of the side's inner face, under the top panel (or the rear rail of a Rails top),
and the wall rail `hw.wall_rail` (`HardwareSelection::wallRail`, default `hw.wall_rail_generic`) with length =
`width` (`HardwareInstance::length`, priced per metre by production), position (0, `depth`, `interior_top` − 20) and the note "Wall rail <width> mm (cut on site) …".
**Back rule:** the hanger body and the rail need `back_space_min` (18 mm generic) between the back panel and
the wall. A Grooved back with `groove_distance` ≥ that (the back then lies wholly in front of the hangers, no
notch) or no back is accepted; an Overlay or Inset back, or a smaller `groove_distance`, fails generation with
a message naming the required value (nothing is changed automatically). Example 600 × 720 × 320, Grooved,
`groove_distance` 18: holes on side.left face B at (687, 312) and (655, 312), rail top at z = 682. L-shaped
corners and appliance gaps with a hanger fail (Unsupported); a missing hanger / rail definition fails
(NotFound). The wall-cabinet template is unchanged (Overlay back, no hanger): switching it would change the
geometry of new wall cabinets and need the new definitions in older project libraries. Project files:
`hardware.hanger` (and `hardware.wallRail` when not default) written only when a hanger is set.

## Drawer boxes

`FrontSpec::drawerBox` (`DrawerBoxSpec{slide, bottom, joint}`, DrawerStack only; `std::nullopt` = fronts only) puts a
box behind EVERY front of the stack (unless overridden per drawer, below), carried by two slides of the definition `slide` (kind DrawerSlide; generic
`hw.slide_ball_bearing` (default) or `hw.slide_undermount`, hardware README). Sizing (all from the resolved
graph, so width / depth / drawer-height changes regenerate the box; rules of `hardware/drawer_slides.hpp`):

* **Optional parameters** (add a custom parameter of that name to override; defaults in mm):
  `drawer_box_top_clearance` 25, `drawer_box_bottom_clearance` 15, `drawer_box_rear_clearance` 10,
  `drawer_box_front_setback` 0, `drawer_bottom_groove_offset` 10, `drawer_bottom_groove_depth` 6,
  `drawer_bottom_clearance` 1 (play at the groove bottom). Negative values are refused.
* **Depth**: the box front is at y0 = `drawer_box_front_setback` (+ `front_thickness` for inset fronts). Slide
  length L = largest nominal length with L + depth_allowance ≤ `interior_depth` − y0 − rear clearance (default
  cabinet, depth 560, overlay back: 550). Box length = L − box_length_deduction.
* **Width**: ball-bearing: opening − 2 · side_clearance (600 wide: 564 − 2 · 12.7 = 538.6); undermount:
  opening − inner_width_deduction + 2 · side thickness (sides ≤ max_side_thickness). Centred in the opening.
* **Height**: from max(front bottom, opening bottom) + bottom clearance to min(front top, opening top) − top
  clearance (a middle drawer: front height − 40); at least the slide's `min_box_height`.
* **Parts** (material role `Drawer` = `MaterialAssignment::drawer`, empty → carcass; `DrawerBottom` =
  `drawerBottom`, empty → back material, else drawer; thicknesses from the material library): two sides
  (length along y, role DrawerSide, upper edge `visibleEdge`), inner front and back between the sides
  (role DrawerBack), bottom (role DrawerBottom). `Grooved` bottom: grooves (bottom thickness + `groove_fit`
  wide, `drawer_bottom_groove_depth` deep, through) on the inner faces of all four, lower groove edge
  max(groove offset, slide bottom_recess) above the box underside; the bottom enters each groove by depth −
  play. `NailedUnder`: a full-size bottom under the box (sides H − bottom thickness); refused for slides
  that need a raised bottom (undermount).
* **Per-drawer boxes**: `FrontSpec::drawerBoxes` (`std::vector<std::optional<DrawerBoxSpec>>`). Empty (default)
  = every drawer uses `drawerBox`. Otherwise exactly one entry per drawer front, aligned with `drawerHeights`
  (bottom to top); each entry REPLACES `drawerBox` for that drawer (`std::nullopt` = front only). Another
  size fails generation ("... has 2 per-drawer box entries but 3 drawer fronts ..."). `drawerBoxFor(front, i)`
  returns the effective box of drawer i, `hasDrawerBoxes(front)` whether any drawer has one. Project files:
  key `drawerBoxes` (array, `null` = no box) only when non-empty, so older files stay byte-identical.
* **Corner joints** (`DrawerBoxSpec::joint`, default `Dowel`): each side (face part) is joined to the inner
  front and to the back (edge parts, their end edges on the side's inner face) with `hardware::connectCorner`:
  `Dowel` = `HardwareSelection::dowel` (blind 12 mm face hole in the side, 20 mm edge hole for the generic
  8x30), `Confirmat` = `HardwareSelection::confirmat` (through hole from the side's outer face, pilot hole in
  the end edge), `None` = nothing drilled. Four jobs per box (`hw.joint.<drawer>.box_front|box_back.left|right`),
  scheduled before the slides. Fastener count / spacing: `joint_end_offset` (at most a quarter of the
  fastener zone) and `joint_max_spacing`. GROOVE CLEARANCE RULE (grooved bottom): fasteners are placed only in
  the zone from upper groove edge + `drawer_joint_groove_clearance` (optional parameter, default 5 mm) +
  the largest hole radius of the fastener up to the top of the box, so at least that clearance of solid
  board remains between every joint hole and the groove; a box too low for that zone fails generation.
  COLLISION RULE: joint and slide holes are checked against the holes already in the part
  (`machining::holesCollide`); an intersection is error `furniture.drawer_box.hole_collision` and the
  fitting is not placed. With the generic slides the drawer-member holes (first at 32 / 20 mm from the box
  front) stay clear of the dowels at half the board thickness from the box ends. Panel suitability is checked
  by the hardware rules (the generic 8x30 dowel needs 3 mm edge walls and more than 2 mm under its 12 mm face hole: boards thicker than 14 mm).
  Example (600 cabinet, 16 mm sides 644 high, 3 mm bottom 10 mm up): zone 13 + 5 + 4 = 22 .. 644 → 4 dowels
  at 72, 246, 420, 594 mm above the box underside per corner.
* **Slides**: one `placeDrawerSlides` job per side after all parts exist; a divider as carrying panel limits
  the cabinet holes to the shared-panel depth.
* **Errors** (generation fails, edits are refused; messages name the drawer slot and numbers): usable depth
  too shallow for the shortest slide, box too low, sides too thick for an undermount runner, missing slide
  definition or drawer material. The interpenetration self-check covers the boxes (bottoms lie in grooves).
* Template `templates::drawerUnitWithBoxes()`: `drawerUnit()` with `drawerBox = DrawerBoxSpec{}` (ball-bearing
  slides, grooved bottom, dowelled corners) behind all four fronts; name "Drawer unit with boxes".

## Hinges and shelves

Intermediate hinges (not the top / bottom one) are moved out of the zone `[shelf bottom - c,
shelf top + c]` (`c = hinge_shelf_clearance`, 40 mm) of every shelf that rests against the carrying
panel. The shelves come from the generator's own record (each adjustable shelf with the two panels
bounding it, each fixed shelf with the two panels it is joined to), not from a geometric proximity
guess, so this works at any `shelf_clearance`. A hinge moves to the nearer free zone boundary (lower
on a tie); when none is free, warning `furniture.hinge.near_shelf`.

## Self-check

After generation `findInterpenetrations()` (checks.hpp) compares all part boxes; any shared volume
that does not lie inside a groove of one of the two parts yields the error
`furniture.geometry.interpenetration` naming both parts and the shared volume in mm. This catches
e.g. pinned parameters that no longer fit a changed construction.

## Contoured parts

A part slot can get a parametric outline through `PartOverride::contour` (edit: `setPartContour()`),
a free panel through `PanelDefinition::contour` (`ContourSpec`, contour_spec.hpp). Generation builds a
`domain::PartShape` (ADR-0008) — the finished outline in the part frame — never extra geometry.

* Kinds (stable texts `Rectangle`, `RoundedCorners`, `ArcEdge`, `Custom`): `Rectangle` = no shape (unless it
  has cut-outs); `RoundedCorners` = four radius expressions for corners (0,0), (L,0), (L,W), (0,W) (L1/W1,
  L1/W2, L2/W2, L2/W1; empty = sharp); `ArcEdge` = one edge (L1/L2/W1/W2) replaced by an arc with a rise
  expression, `Convex` or `Concave`; `Custom` = counter-clockwise vertices with expression coordinates and a
  numeric bulge (|b| ≤ 1). `cutouts`: `Rectangle{x, y, length, width, radius}` (min corner, corner radius)
  and `Circle{x, y, radius}` (centre).
* Expressions are evaluated over the cabinet's resolved parameter graph (the panel's own graph) plus the
  read-only names `contour.length`, `contour.width`, `contour.thickness` of the shaped part (they win
  over graph names). Errors start with `furniture.contour.invalid:` and name the part / slot, the field
  and the expression; they make `generate()` (and with it every edit) fail, so an invalid spec is
  rejected and the definition stays unchanged.
* **Size rule.** The part keeps its construction-derived L × W and the outline's bounding box is exactly
  [0,L]×[0,W]. A **convex arc is cut INTO the box**: the ends of the edge move inwards by the rise and the
  apex lies on the original edge line (e.g. L2 of 800 × 720, rise 50: ends at y = 670, apex (400, 720),
  bulge = rise / half chord = 50 / 400 = 0.125, R = 1625). A concave arc runs between the original
  corners. Rise: > 0, ≤ half the edge, < the part's other dimension. Custom outlines must span the box.
* **Edge bands** move from `Part::edges` (generator roles + overrides) to `shape->outerEdges` and
  `Part::edges` is cleared: a straight segment on a box side takes that side's band; an ArcEdge arc the band
  of the edge it replaces; a RoundedCorners corner arc the band of its L side (L1/L2) when banded, else of
  its W side (so the banded front L1 of a shelf continues around both front arcs); Custom segments use
  `bandFrom` when set, else the box side they lie on, else raw. Cut-outs are never banded.
* **Machining.** Contours are applied after hardware placement; every operation whose footprint leaves the
  outline or enters a cut-out is an Error `furniture.contour.machining_outside` (kept, not dropped), e.g. a
  confirmat hole in a removed R 100 corner, or edge holes under a concave arc (`checkOperationsInsideOutline`).
* **Self-check.** Shapes only remove material inside the part's box, so the box-based interpenetration
  check stays valid (conservative) and runs before the contours are applied.
* A contour on a slot the cabinet does not generate gives the warning `furniture.contour.unused`.
* Limits: 500 Custom vertices, 50 cut-outs per contour, ~10 000 contour segments per cabinet
  (`checkDefinitionLimits`).
* Template `templates::openShelvingRounded()`: the open shelving unit whose four adjustable shelves have
  rounded front corners of radius `shelf_corner_radius` (custom parameter, 30 mm).

## Limits

`limits.hpp` holds hard limits, far above real furniture, so that a hostile or corrupted definition (e.g. from a
project file) cannot make generation run for minutes or produce infinite geometry. `generate()` checks them first
(`checkDefinitionLimits`), so every edit of edit.hpp, the project commands and the project file loader refuse the
same definitions with the same messages (ConstraintViolation / InvalidArgument naming the section path or
parameter, the value and the limit).

| Limit | Value |
|---|---|
| `kMaxDimension`: every standard parameter (all are lengths), Fixed section / drawer sizes, Expression sizes as evaluated, and the length / width / thickness of every generated part | finite, \|v\| ≤ 100 000 mm (parts also > 0) |
| `kMaxWeight`: Weight sizes | ≤ 1 000 000 |
| `kMaxShelvesPerSection` | 100 |
| `kMaxShelvesPerCabinet` (adjustable shelves of all sections) | 200 |
| `kMaxSectionChildren` | 50 |
| `kMaxSections` (root included) | 250 |
| `kMaxSectionDepth` (root = 0) | 16 |
| `kMaxDrawerHeights` (fronts per drawer stack) | 30 |
| `kMaxPartsPerAssembly`: `estimatePartCount()` = 8 + dividers + shelves + fronts, and the generated count | 2 000 |

A generated part that is not finite, not > 0 or longer than 100 000 mm (e.g. a pinned `top_length` of -5) fails
generation with a message starting `furniture.geometry.invalid_size:` before any hardware is placed.

Why shelves have a per-cabinet limit: all pins of the shelves resting against one panel are drilled into that
panel, and `machining::validateOperations` compares every pair of holes of a part (quadratic in holes per part).
With 200 shelves a side panel carries at most ~400 pin holes plus joints.

Performance: part lookups by slot are O(1) (`Gen::slotIndex`), and hardware placements are stored without the
assembly-wide scans of `hardware::applyPlacement` (equivalent result for the generator's always-new instance ids).
The largest allowed assemblies — 50 columns of 30-drawer stacks (1554 parts) or 200 shelves on one pair of sides —
generate in well under a second even unoptimised (`limits_tests.cpp`, bound 5 s, measured ~0.05 s).

## Diagnostics

See generate.hpp for the full list of codes.

## Limitations

* Drawer boxes: no rear hook hole / back notch for undermount runners, no fixing of the drawer front to the box
  (front adjusters / screws), nailed bottoms get no nail positions, rectangular boxes only; slide hole rows are
  taken from the generic definitions.
* The interpenetration check compares bounding boxes (exact for the axis-aligned panels generated here).
* Hinge heights are not snapped to a 32 mm system row.
* Contours: hardware rules do not know the outline (a fitting in a removed area is reported, not moved);
  cut-outs are not banded; one kind per part (no rounded corners combined with an arc edge).

## Corner cabinets (`CabinetDefinition::corner`, `CornerSpec`)

Templates `templates::blindCornerCabinet()` / `templates::lCornerCabinet()` (CLI `fos new --template
corner-blind|corner-l`). Corner sizes are custom graph parameters (`corner_param` in definition.hpp); a missing one
fails generation. Door width limits `kMinCornerDoorWidth` 150 / `kMaxCornerDoorWidth` 600 mm and
`kMinBlindPanelWidth` 50 mm are generic rules of thumb, not manufacturer data.

**Blind (`CornerKind::Blind`, `blindSide` Left / Right).** An ordinary carcass whose root front (must be a single
`Door`, hinged on the side away from the blind panel) is split at `blind_width`, measured from the outer face of the
blind-side carcass side to the centre of the gap: slot `blind` (role Generic, front material, same frame and height
as the door; no fittings are generated — it is screwed on from inside) and `door`. Template 1000 x 820 x 560,
blind left 500: blind panel x 2..498.5, door x 501.5..998 (496.5 mm) hinged right. Errors name `blind_width`, the
panel / door width and the limit.

**L-shaped (`CornerKind::LShape`).** Leg A = `width` x `depth` along the rear wall (front at y = 0), leg B =
`corner_width_b` x `corner_depth_b` along the left wall (x = 0), towards -Y down to yF = depth - corner_width_b.
Slots: `side.a` (end of leg A, at x = width), `side.b` (end of leg B, at y = yF, width -> +X, L2 = front edge),
`bottom`, `top` and `shelf.<i>` (L-shaped `PartShape`, 6 straight segments, set at generation; the two front
segments carry the band of the part's L1 edge, override L1 to change it), `plinth.a|b` (SidesToFloor), `back.a|b`
(Overlay; A runs over the end of B at the wall corner) and `door.a` / `door.b`. Shelves: shelf_clearance from both
end sides, front edges shelf_setback behind both fronts, rear edges on the wall lines; supported by 2 pins in each
end side only (`hw.pins.shelf.<i>`, holes drilled by the generator with the shelf-pin catalogue dimensions; no
mid support at the wall corner is modelled — a large shelf may need one). Doors: the Full-overlay `DoorPair` of the
root is generated as TWO INDEPENDENT doors with generic cup hinges on their end sides (bi-fold corner hinges are not
modelled): door A x from corner_depth_b + front_offset + front_thickness + front_gap to width - front_reveal, door B
in the plane x = corner_depth_b + front_offset .. + front_thickness, from yF + front_reveal to flush with door A's
outer face. Joints: side/bottom and side/top corners with the selected fasteners (the butt contact of the box is the
real L edge), plinth to side with `dividerFastener`. Unsupported (generation error): OverSides joints, stretcher
rails, Grooved / Inset backs, feet, dividers, other front kinds. The interpenetration self-check compares parts with
a rectilinear outline by the rectangles of that outline (checks.hpp), so door B inside the bounding box of the L
panels is not reported. Project files: key `corner: {kind, blindSide?}` written only for corner cabinets.

## Appliance housings

All appliance sizes here are GENERIC defaults. The niche dimensions, gap width and ventilation
an appliance needs are given in its installation sheet: check it. Nothing in this module claims
compatibility with any particular appliance model.

* **Open niche** (`FrontKind::ApplianceNiche`, `FrontSpec::niche` = `ApplianceNicheSpec`): a leaf
  section without shelves and without a front part. Its clear height is the section's own size
  (the template uses `SectionSize::expr("niche_height")`); the panel below it (fixed shelf of the
  parent split, or the carcass bottom) is the shelf the appliance stands on. Ventilation:
  `ShelfSlot` = rectangular cut-out in that panel, `ventSize` deep, its rear edge
  `ventEdgeDistance` in front of y = `interior_depth`, ending `ventEndMargin` from the panels on
  both sides; `BackCutout` = rectangular cut-out in the back panel, `ventSize` high, its top
  `ventEdgeDistance` below the top of the niche, same end margins (warning
  `furniture.appliance.no_back` and no cut-out without a back). The cut-outs are added to the
  part's override contour, if any, after hardware placement (`furniture.contour.machining_outside`
  reports a fitting inside the opening).
* **Decor front** (`FrontKind::ApplianceFront`): generated like a single door over the opening
  (slot `appliance_front[.<path>]`, role Door), never with hinges, with a horizontal handle per
  `FrontSpec::handle`, and `Part::custom["furniture.mounting"] = "fixed to appliance door"`.
* **Appliance gap** (`CabinetDefinition::applianceGap`, `ApplianceGapSpec`): no carcass; end
  panel(s) `end_panel.left` / `end_panel.right` (H x (depth + front_offset + front_thickness) x
  thickness, from the floor, flush with the front faces) and the decor front over the gap: x with
  `front_reveal` towards a neighbouring cabinet and `front_gap` towards an end panel, z from
  `carcass_bottom + front_reveal` to `height - front_reveal`. Warning
  `furniture.appliance.gap_width` when the gap differs from the custom parameter
  `appliance_gap_width`. No plinth strip, worktop or appliance fixing hardware is generated.
* Templates: `ovenTallHousing()` (drawers 700 / niche 595 / door section) and `dishwasherFront()`
  (600 gap, right end panel, width = `appliance_gap_width + thickness`).
