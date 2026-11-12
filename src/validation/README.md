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

### 2. edge-banding (`checkEdgeBanding`)
| Code | Severity | Condition |
|---|---|---|
| `validation.edge.missing_band` | error | edge refers to an edge band not in `Model::edgeBands` |
| `validation.edge.band_too_narrow` | error | band width < part thickness |
| `validation.edge.band_too_thick` | warning | band thickness > `maxBandThicknessShare` x perpendicular dimension (width for L1/L2, length for W1/W2) |
| `validation.edge.non_positive_cut_size` | error | cut length or width <= 0 after subtracting the bands |
| `validation.edge.invalid_band` | error | library band with thickness < 0 / non-finite or width <= 0 (`entityId` = band id) |

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

Same SAT/sweep as check 3, no groove allowance. Stats: `possibleCrossAssemblyPartPairs`,
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

## Options (`ValidationOptions`)
| Option | Default | Meaning |
|---|---|---|
| `checkParts` ... `checkCompleteness` | true | enable the seven checks |
| `minPartSize` | 10 mm | `part.too_small` threshold |
| `maxBandThicknessShare` | 0.25 | `edge.band_too_thick` threshold |
| `interferenceTolerance` | 0.01 mm | accepted overlap (touching) |
| `grooveFitTolerance` | 0.1 mm | accepted protrusion beyond a groove box |
| `assemblyInterferenceSeverity` | Warning | severity of check 4 |
| `coaxialTolerance` | 0.1 mm | max axis distance of mating holes |
| `matingSearchRadius` | 8 mm | pairing radius (heuristic) |
| `matingMaxAxialGap` | 1 mm | pairing axial gap (heuristic) |
| `matingParallelToleranceDeg` | 1 deg | pairing parallelism (heuristic) |
| `machining` | defaults | forwarded to `machining::validateOperations` |

## Limitations
* Parts are boxes: pockets, holes and grooves do not remove material in the interference
  test (except the groove engagement rule above).
* Groove recognition only for axis-aligned parts and axis-parallel grooves.
* Mating-hole pairing is a heuristic; holes from `"user"` operations are never paired.
* Hole/groove and groove/groove collisions inside one part are not checked (machining module scope).
