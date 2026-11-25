# validation — model-level checks

`fos::validation` checks a resolved `domain::Model` (read-only) and returns a sorted
`ValidationReport` of `Finding`s (a `core::Diagnostic` plus `assemblyId`, `partId`,
`relatedIds`). It never throws and never crashes on nonsensical input (NaN sizes, non-rigid
placements, dangling material / edge band / hardware / part ids). Units: mm, degrees.

```cpp
#include "fos/validation/validation.hpp"
fos::validation::ValidationReport r = fos::validation::validateModel(model, options);
if (!r.ok()) std::cout << fos::validation::toText(r);   // or toMarkdown(r)
```

Headers: `validation.hpp` (`validateModel`, `availableChecks`), `options.hpp`
(`ValidationOptions`), `report.hpp` (`Finding`, `ValidationReport`, `ValidationStats`, filters
`findingsForEntity / findingsWithCode / findingsWithSeverity`, writers `toText / toMarkdown`),
`obb.hpp` (oriented boxes, exact separating-axis test).

Report order (deterministic): severity (error, warning, info), then `assemblyId`, `partId`,
`code`, `message`, `entityId`. Messages name parts by their name (`'Shelf'`), or by id when
unnamed, and give the numbers; lengths are printed with 0.1 mm precision, values below 1 mm
with up to 3 decimals.

## Checks and codes

Every check is a registry entry (`availableChecks()`), switched by its `ValidationOptions` flag.
`entityId` is the part id unless stated otherwise.

### 1. parts (`checkParts`)
| Code | Severity | Condition |
|---|---|---|
| `validation.part.invalid_size` | error | length, width or thickness not finite or <= 0 |
| `validation.part.non_rigid_placement` | error | placement not finite or not a proper rotation (tolerance 1e-6); part excluded from interference |
| `validation.part.missing_material` | error | no material id, or id not in `Model::materials` |
| `validation.part.thickness_mismatch` | error | part thickness != material thickness (both numbers in the message) |
| `validation.part.duplicate_id` | error | two parts of one assembly share an id |
| `validation.part.too_small` | warning | finished length or width < `minPartSize` |
| `validation.part.exceeds_sheet` | error | `domain::cutSize` does not fit the material sheet: material `hasGrain` and part grain `AlongLength` -> cut length along sheet length only; `AlongWidth` -> rotated only; otherwise either orientation |
| `validation.assembly.duplicate_id` | error | two assemblies share an id (`entityId` = assembly id) |
| `validation.assembly.non_rigid_placement` | error | assembly placement not rigid; excluded from check 4 |

Contoured parts (ADR-0008), checked for every shaped part with a valid size. Segment and
cut-out numbers in messages are 0-based indices into `PartShape::outer` / `cutouts`.

| Code | Severity | Condition |
|---|---|---|
| `validation.shape.invalid_outline` | error | `geometry::validateContour(outer)` fails (message of the geometry kernel appended); other shape checks skipped |
| `validation.shape.clockwise_outline` | error | outline signed area < 0 |
| `validation.shape.bounds_mismatch` | error | exact outline bounding box differs from [0,L]x[0,W] by more than 1e-6 mm |
| `validation.shape.invalid_cutout` | error | a cut-out fails `validateContour` |
| `validation.shape.cutout_outside` | error | a cut-out touches / crosses the outline or lies outside it |
| `validation.shape.cutouts_overlap` | error | two valid cut-outs touch, cross or nest |
| `validation.shape.edges_with_shape` | error | shaped part with a band in `Part::edges` (must be empty) |
| `validation.shape.outer_edges_count` | error | `outerEdges` neither empty nor one entry per outline segment |
| `validation.shape.cut_contour` | error | `domain::cutContour` fails (geometry message appended); only when outline, bounds, band count and `Part::edges` are fine |

### 2. edge-banding (`checkEdgeBanding`)
| Code | Severity | Condition |
|---|---|---|
| `validation.edge.missing_band` | error | edge refers to an edge band not in `Model::edgeBands` |
| `validation.edge.band_too_narrow` | error | band width < part thickness |
| `validation.edge.band_too_thick` | warning | band thickness > `maxBandThicknessShare` x perpendicular dimension (width for L1/L2, length for W1/W2) |
| `validation.edge.non_positive_cut_size` | error | cut length or width <= 0 after subtracting the bands |
| `validation.edge.invalid_band` | error | library band with thickness < 0 / non-finite or width <= 0 (`entityId` = band id) |
| `validation.edge.radius_too_small` | warning | contoured part: a banded outline arc whose substrate radius (R - t convex, R + t concave; t = band thickness) is below `edgeBandMinBendRadiusFactor` x t. GENERIC, UNVERIFIED rule of thumb (default 10: 20 mm for 2 mm ABS) |

For contoured parts `missing_band` and `band_too_narrow` are also checked per outline segment
("outline segment i"); `band_too_thick` and `non_positive_cut_size` apply to `Part::edges` only
(the raw outline is checked by `validation.shape.cut_contour`).

### 3. part-interference (`checkPartInterference`)
| Code | Severity | Condition |
|---|---|---|
| `validation.interference.parts` | error | two parts of one assembly share volume |
| `validation.interference.outside_groove` | error | a part engages a groove of the other part but sticks out of the groove box by more than `grooveFitTolerance` |

* Exact test: separating-axis theorem on the two oriented part boxes (15 axes, `obb.hpp`).
  The reported depth is the penetration depth (shortest separating translation) and the
  axis is named `X`/`Y`/`Z` when (anti)parallel to an assembly axis, else printed as a unit
  vector `(x, y, z)`. Overlaps up to `interferenceTolerance` (touching) are accepted.
  The part named as penetrating is the one with the larger extent along that axis (a shelf
  end entering a side); ties: the part listed first.
* Contoured parts (ADR-0008, option `exactShapeInterference`, default on): when the boxes
  interfere and at least one part has a valid non-rectangular shape inside [0,L]x[0,W], the
  pair is re-tested with the exact outline (lines and arcs, no tessellation). Criterion: the
  solids, each eroded by `interferenceTolerance / 2`, still intersect. A part is the prism
  "outline minus cut-outs" x [0, T]; its erosion is the outline offset inwards and the cut-outs
  offset outwards (`geometry::offsetContour`) x [h, T - h]; a box shrinks by h.
  * shaped vs box, ANY relative orientation: the eroded box is clipped to the shaped part's
    eroded thickness slab and projected onto its plane (convex polygon), then tested against
    the eroded region - exact.
  * two shaped parts with (anti)parallel thickness axes: both regions in one plane (mirrored
    when anti-parallel) plus overlap of the eroded thickness intervals - exact.
  * two shaped parts at another relative orientation: each shape against the other's BOX; the
    pair is reported only if both say so - conservative fallback (may report, never misses).
  Reported depth = diameter of the largest ball that fits into the shared material (bisection
  on the erosion, 0.001 mm), capped by the box depth; the message also gives the box result.
  "at least" marks a depth whose search stopped because an outline arc could not be eroded
  further. offsetContour uses miter joins at sharp corners, whose erosion is a subset of the
  exact one: a reported overlap is always real, near a sharp cut-out corner (or reflex outline
  corner) a sliver thinner than about 0.41 x tolerance may be missed and the depth may be
  slightly under-estimated. If the base erosion is not representable the box result stands.
  Stats: `shapedPairsRefined`, `shapedPairsCleared`. Groove engagement is evaluated after the
  refinement on the boxes as before.
* Broad phase: sweep and prune of the part AABBs along X. Stats: `possiblePartPairs`
  (n(n-1)/2 per assembly), `broadPhaseChecks` (pairs reaching the AABB comparison),
  `partPairsTested` (AABBs overlap -> exact SAT test).
* Groove engagement — exactly what is recognised: both parts have axis-aligned placements
  (axes parallel to the assembly axes); the groove is a `domain::Groove` whose centre line is
  parallel to u or v of its face. Its swept box is the centre line +- width/2 across (no
  extension beyond the end points), from the face surface to `depth` into the material,
  mapped to the assembly frame with `machining::faceToAssembly`. If the AABB intersection of
  the two parts lies inside that box (+ `grooveFitTolerance` per axis), the pair is accepted
  and counted in `grooveEngagementsAllowed`. If it overlaps a groove box but protrudes
  further, `outside_groove` names the largest protrusion and its axis. Rotated parts,
  diagonal grooves, pockets and holes are not recognised (plain interference is reported).

### 4. assembly-interference (`checkAssemblyInterference`)
| Code | Severity | Condition |
|---|---|---|
| `validation.interference.assemblies` | `assemblyInterferenceSeverity` (warning) | parts of different assemblies share volume in the project frame (assembly placement applied); `relatedIds` = other part, other assembly |

Same SAT/sweep and contoured-part refinement as check 3, no groove allowance. Stats: `possibleCrossAssemblyPartPairs`,
`crossAssemblyBroadPhaseChecks`, `crossAssemblyPartPairsTested`.

### 5. machining (`checkMachining`)
* All `machining.*` codes of `machining::validateOperations` (options `machining`), per part
  with a valid size, with `assemblyId`/`partId` filled in.
* `validation.machining.mating_holes_not_coaxial` (error). HEURISTIC pairing: two holes on
  different parts with the same `source` (not `"user"`) are a mating pair when their axes are
  parallel within `matingParallelToleranceDeg`, the midpoint of the second drilled segment is
  closer than `matingSearchRadius` to the first axis and the drilled segments are at most
  `matingMaxAxialGap` apart along the axis. A pair whose axis distance exceeds
  `coaxialTolerance` is reported with the distance. Pairs counted in `matingHolePairs`.

### 6. hardware (`checkHardware`) — `entityId` = instance id
| Code | Severity | Condition |
|---|---|---|
| `validation.hardware.unknown_definition` | error | definition not in `Model::hardwareCatalog` |
| `validation.hardware.unknown_part` | error | instance refers to a part not in its assembly |
| `validation.hardware.invalid_quantity` | error | quantity <= 0 |
| `validation.hardware.unverified_dimensions` | info | ONE per used definition with `dimensionsVerified == false` (`entityId` = definition id) |

### 7. completeness (`checkCompleteness`)
| Code | Severity | Condition |
|---|---|---|
| `validation.completeness.unnamed_part` | warning | part name empty |
| `validation.completeness.empty_assembly` | warning | assembly without parts (`entityId` = assembly id) |
| `validation.completeness.material_sheet_size` | error | library material with sheet length/width not finite or <= 0 (`entityId` = material id) |
| `validation.completeness.material_thickness` | error | library material thickness not finite or <= 0 |
| `validation.completeness.unknown_cost` | info | material used by a part has no cost per sheet |

### 8. engineering (`checkEngineering`, OFF by default; CLI `fos validate --engineering`)
HEURISTICS with generic, documented assumptions — groundwork for roadmap phase 17, not a
structural calculation and not a manufacturer specification. Findings are Warning or Info,
never Error, and every message names the numbers and the assumption used. Each rule has its
own switch in `ValidationOptions::engineering` (`EngineeringOptions`). g = 9.81 N/kg.

| Code | Severity | Condition |
|---|---|---|
| `validation.engineering.shelf_sag` | warning | estimated mid-span sag of a `PartRole::Shelf` part > sag limit |
| `validation.engineering.unknown_modulus` | info | shelf material of kind Other without custom `elastic_modulus_mpa` (not estimated) |
| `validation.engineering.invalid_custom_value` | info | custom `elastic_modulus_mpa` not a positive number (not estimated) |
| `validation.engineering.too_few_hinges` | warning | hinges on a door < suggested count |
| `validation.engineering.hinge_table_exceeded` | info | hinged edge longer than the last row of the hinge table |
| `validation.engineering.drawer_too_deep` | warning | drawer box length + `drawerMinRearClearance` > cabinet interior depth |
| `validation.engineering.slide_too_long` | warning | drawer slide length > cabinet interior depth (`relatedIds` = slide instance) |
| `validation.engineering.drawer_longer_than_slide` | warning | drawer box length > its slide length |
| `validation.engineering.tipping_risk` | warning | assembly height/depth > `tippingRatio`, height >= `tippingMinHeight`, no wall fixing (`entityId` = assembly id) |

**Shelf sag.** Simply supported beam over the shelf LENGTH (supports at both short ends),
uniformly distributed load: `delta = 5 W L^3 / (384 E I)`, `I = b t^3 / 12` (b = shelf width,
t = thickness, mm), `W = (shelfLoadKg + self weight) * 9.81` N, E in MPa (N/mm^2) -> delta in
mm. Default load 15 kg per shelf (a generic assumption, e.g. a row of books), self weight
added (`shelfSelfWeight`). Limit `min(span / sagLimitSpanRatio, sagLimitMm)` = `min(L/300, 2 mm)`;
a term <= 0 is dropped. No creep (long-term sag of particleboard/MDF under permanent load is
noticeably larger), no edge-band or front-rail stiffening, no point loads.
E: material custom `"elastic_modulus_mpa"` if present, else a GENERIC table by `MaterialKind`
(order-of-magnitude typical values of the kind found in board data sheets and the minimum
requirements of EN 312 / EN 622-5, chosen on the low side; not values of any product):
Particleboard 2500, MDF 3000, HDF 4000, Plywood 6000, SolidWood 9000, Glass 70000 (soda-lime
textbook value) MPa; Other -> unknown (info).
Density (self weight and door weight): `Material::density` if > 0, else generic Particleboard
650, MDF 750, HDF 900, Plywood 600, SolidWood 600, Glass 2500 kg/m3; Other -> unknown (term skipped).

**Hinge count.** Hinges on a door = sum of `quantity` of Hinge instances listing the door in
`parts`; doors without any hinge instance (sliding, lift, pivot) are not judged. Hinged edge =
the door's extent along Z, or along X when a hinge is carried by a horizontal panel
(top-hinged flap). Suggested = max(table count, max(2, ceil(door mass / `doorKgPerHinge`))).
RULE OF THUMB, check the hinge maker's chart: <= 900 mm: 2, <= 1600: 3, <= 2000: 4, <= 2400: 5
(`hingeHeightTable`, same numbers as the hardware module's medium column); 5 kg per hinge.

**Drawer depth.** Interior depth along Y = from the front edge of the `Side` parts to the
front face of the `Back` panel (when it lies within the sides), else to the rear edge of the
sides; assemblies without sides are skipped. Box length = extent of each `DrawerSide` part
along Y (drawer runs along Y). Slide length of a DrawerSlide instance that lists the drawer
side: definition param `length` (fixed-length definition), else the note
`"slide length <L> mm"` written by `hardware::placeDrawerSlides`; unknown -> slide rules skipped.
Tolerance 0.1 mm on all comparisons.

**Tipping.** Assembly bounding box (valid parts, assembly frame): height = Z, depth = Y.
Warns when height >= 1000 mm and height/depth > 2.0 and no hardware instance uses a
Suspension definition (cabinet hanger) or a definition with custom `wall_fixing` = `"true"`
(e.g. an anti-tip bracket). Not a stability calculation (no centre of gravity, no loads,
no doors/drawers opened).

## Options (`ValidationOptions`)
| Option | Default | Meaning |
|---|---|---|
| `checkParts` ... `checkCompleteness` | true | enable checks 1-7 |
| `checkEngineering` | false | enable check 8 (heuristics) |
| `engineering.shelfSag` / `hingeCount` / `drawerDepth` / `tipping` | true | individual engineering rules |
| `engineering.shelfLoadKg` / `shelfSelfWeight` | 15 kg / true | shelf load assumption |
| `engineering.sagLimitSpanRatio` / `sagLimitMm` | 300 / 2 mm | sag limit min(L/ratio, mm); <= 0 drops a term |
| `engineering.hingeHeightTable` / `doorKgPerHinge` | see check 8 / 5 kg | hinge rule of thumb; <= 0 drops the weight term |
| `engineering.drawerMinRearClearance` | 0 mm | space required behind a drawer box |
| `engineering.tippingRatio` / `tippingMinHeight` | 2.0 / 1000 mm | tipping thresholds |
| `minPartSize` | 10 mm | `part.too_small` threshold |
| `maxBandThicknessShare` | 0.25 | `edge.band_too_thick` threshold |
| `interferenceTolerance` | 0.01 mm | accepted overlap (touching) |
| `grooveFitTolerance` | 0.1 mm | accepted protrusion beyond a groove box |
| `assemblyInterferenceSeverity` | Warning | severity of check 4 |
| `exactShapeInterference` | true | exact-outline refinement for contoured parts (checks 3, 4) |
| `edgeBandMinBendRadiusFactor` | 10 | `edge.radius_too_small` threshold factor (generic, unverified); <= 0 disables |
| `coaxialTolerance` | 0.1 mm | max axis distance of mating holes |
| `matingSearchRadius` | 8 mm | pairing radius (heuristic) |
| `matingMaxAxialGap` | 1 mm | pairing axial gap (heuristic) |
| `matingParallelToleranceDeg` | 1 deg | pairing parallelism (heuristic) |
| `machining` | defaults | forwarded to `machining::validateOperations` |

## Limitations
* Parts are boxes or exact contoured prisms (cut-outs remove material); pockets, holes and
  grooves do not remove material in the interference test (except the groove engagement rule).
* Groove recognition only for axis-aligned parts and axis-parallel grooves.
* Mating-hole pairing is a heuristic; holes from `"user"` operations are never paired.
* Engineering rules (check 8) are heuristics with generic constants; no creep, no load
  ratings of real fittings, no centre-of-gravity stability analysis.
* Hole/groove and groove/groove collisions inside one part are not checked (machining module scope).
