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
`drawer.<path>.<i>` (bottom to top) and, with a drawer box, `drawer.<path>.<i>.side.left|side.right|box_front|box_back|bottom`.
Section paths: root `""`, children `1`, `2`, `2.1`, ... Empty
path segments are skipped (`shelf.1` for the root's first shelf). Ids depend only on the tree
position, so changing a shelf count only adds/removes the trailing ids of that section.
Hardware instances: `hw.joint.<edge slot>.<face slot>`, `hw.pins.<shelf slot>`,
`hw.hinges.<door slot>`, `hw.handle.<front slot>`, `hw.slide.<drawer slot>.left|right`, `hw.feet`; back grooves
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

## Drawer boxes

`FrontSpec::drawerBox` (`DrawerBoxSpec{slide, bottom}`, DrawerStack only; `std::nullopt` = fronts only) puts a
box behind EVERY front of the stack, carried by two slides of the definition `slide` (kind DrawerSlide; generic
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
* **Slides**: one `placeDrawerSlides` job per side after all parts exist; a divider as carrying panel limits
  the cabinet holes to the shared-panel depth.
* **Errors** (generation fails, edits are refused; messages name the drawer slot and numbers): usable depth
  too shallow for the shortest slide, box too low, sides too thick for an undermount runner, missing slide
  definition or drawer material. The interpenetration self-check covers the boxes (bottoms lie in grooves).

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

* Drawer boxes: no box corner joints (dowels / screws) are drilled, no rear hook hole for undermount runners,
  one box option per stack, rectangular boxes only; slide hole rows are taken from the generic definitions.
* Inset doors get no hinges (no rule available; warning `furniture.front.inset_hinges`).
* The interpenetration check compares bounding boxes (exact for the axis-aligned panels generated here).
* Hinge heights are not snapped to a 32 mm system row.
* Contours: hardware rules do not know the outline (a fitting in a removed area is reported, not moved);
  cut-outs are not banded; one kind per part (no rounded corners combined with an arc edge).
