# fos::cutting — rectangular guillotine cutting optimisation

Plans how rectangular panels are cut from sheet material on a panel saw, where every cut
runs edge to edge through the piece of material being cut (guillotine cuts).

**The optimiser is a heuristic.** It returns a good, valid layout quickly; it does not
search for and never claims an optimal one (`CuttingResult::isProvenOptimal` is always
`false`). The only thing it can prove is described under *Lower bound* below.

| Header | Contents | Depends on |
|---|---|---|
| `fos/cutting/types.hpp` | `Job`, `Piece`, `StockSheet`, `Trim`, `CuttingResult`, `SheetLayout`, `Cut`, `CutNode`, … | — |
| `fos/cutting/optimizer.hpp` | `optimize()`, `validateJob()`, `usableArea()`, `isReusableRemnant()`, `areaLowerBoundOnSheetCount()` | core |
| `fos/cutting/validate.hpp` | `validateLayout()` — independent checker | core |
| `fos/cutting/svg.hpp` | `renderSheetSvg()`, `xmlEscape()` | core |
| `fos/cutting/from_model.hpp` | `jobsFromModel()` — adapter from `domain::Model` (target **`fos::cutting_model`**) | cutting, domain |

Two CMake targets: `fos::cutting` (optimiser core, links **fos::core only**) and
`fos::cutting_model` (`from_model.cpp`; links `fos::cutting` + `fos::domain`). Code that calls
`jobsFromModel()` must link `fos::cutting_model`. The grain/rotation rule comes from
`domain::canRotateOnSheet()` / `domain::sheetAlignment()` (shared with `production`). An edge that
references an edge band missing from the model yields `cutting.adapter.edgeband_missing` (Warning).

## Model

* **Sheet frame.** Origin at a sheet corner, +X along the sheet *length* (the grain
  direction of grained boards), +Y along the sheet *width*. Millimetres everywhere.
* **Job** = pieces of one material/thickness + stock sheet types + saw settings.
* **Piece**: `length × width`, `quantity`, `canRotate`. With `canRotate == false` the piece
  `length` must run along the sheet length (grain); otherwise it may be turned by 90°.
  `Placement::length` is always the placed extent along X, so a rotated placement has
  `length == piece.width`.
* **StockSheet**: size, `quantity` (or `kUnlimited`), `cost`, `isRemnant`, `priority`
  (higher is opened first).
* **Cut orientation.** `Lengthwise`: cut line parallel to X, `position` is its y.
  `Crosswise`: cut line parallel to Y, `position` is its x.

### Trim — exact semantics

`Trim{left, right, top, bottom}` is removed from the sheet first (left: x = 0 side, right:
x = length side, bottom: y = 0 side, top: y = width side). What remains is the **usable
area** `[left, L − right] × [bottom, W − top]`; all pieces lie inside it. The trim strips
include their own saw cut: a trim cut is reported at the usable-area boundary with
`kerfWidth == 0` and `kind == CutKind::Trim`, and **a piece flush with the usable-area
boundary needs no kerf on that side**. Trim cuts come first in the cut sequence (left,
right, bottom, top; a zero trim produces no cut).

### Kerf — exact semantics

A split cut at `position` through a piece of material spanning `[start, end]` removes the
strip `[position, position + kerfWidth]` and leaves a low part `[start, position]` and a
high part `[position + kerfWidth, end]`.

* `kerfWidth == job.kerf`, except when less than one kerf of material remains beyond
  `position`; then the blade only removes what is there (`kerfWidth == end − position`) and
  there is no high part.
* No cut is made (and no kerf is charged) where a piece already ends at the edge of the
  material: a piece exactly as large as the remaining strip fits. Example with kerf 4 on a
  2000 mm long sheet: `998 + 4 + 998 = 2000` fits; `1000 + 4 + 1000` does not.
* Consequently two placed pieces are always at least one kerf apart along X or along Y.
* Sizes are compared with `core::kLengthEps` (1e-6 mm); nothing is rounded.

### Stages

Consecutive cuts of the same orientation along a branch of the cut tree form one stage;
the first split cut on a sheet is stage 1, trim cuts are stage 0. Cutting a piece to exact
size inside a strip counts as a stage like any other cut. `Job::maxStages` (0 = unlimited,
otherwise ≥ 2) bounds the highest stage. `Job::firstCut` is a *preference*: it decides the
orientation of the first split cut whenever the first piece on a sheet leaves the choice
(i.e. needs a cut in both directions).

### Result

Per sheet: placements, the binary guillotine cut tree (`tree`, node 0 = usable area), the
flat executable cut sequence (`cuts`: trim cuts, then the tree in pre-order — a cut, then
everything on its low side, then its high side; each cut carries the rectangle `source` it
splits, its `extent` and `stage`), offcuts and areas. Area identity:

    sheetArea = trimArea + usedArea + kerfArea + remnantArea + wasteArea

* `usedArea` — sum of the placed piece areas.
* `kerfArea` — material removed by split cuts (`kerfWidth × extent` per cut).
* An **offcut** is a leaf of the cut tree without a piece. It is a *reusable remnant* when
  its longer side ≥ `minRemnantLength` and its shorter side ≥ `minRemnantWidth`
  (defaults 300 × 100), otherwise *waste*.
* `utilisationOfSheetPct = 100 · usedArea / sheetArea`;
  `utilisationOfUsablePct = 100 · usedArea / usableArea`. The totals use the sums over all
  sheets.

Pieces that are not in the layout are listed in `unplaced` with a reason and a complete
sentence, e.g. `Piece 'Tall side' 2900 × 600 does not fit the usable area 2780 × 2050 of
any stock sheet.` (`DoesNotFitAnyStock`) or `StockExhausted`.

## Algorithm (heuristic)

1. **Multi-strategy construction.** Every combination of
   * piece order: decreasing area, longer side, shorter side, perimeter, length;
   * split rule for the L-shaped leftover around a placed piece: shorter leftover axis,
     longer leftover axis, max area, min area, always lengthwise first, always crosswise first;
   * free-rectangle choice: best area fit, best short-side fit;
   * orientation: best of both, prefer unrotated, prefer rotated (only if something may rotate);
   * stock choice (only with several stock types): largest first, smallest first, smallest
     that still covers the remaining piece area

   is run once (180 runs for one stock type, 540 for several). A run places each piece in
   the first open sheet with a fitting free rectangle (first-fit over sheets, best-fit
   within a sheet) and opens a new sheet only when necessary.
2. **Guillotine-valid re-splitting ("remnant merging").** The two leftover rectangles
   around a piece can be cut in two ways. As long as both are untouched, the packer may
   swap the order of the two cuts — to make room for a piece that otherwise would not fit,
   and at the end to keep the variant with more reusable remnant area (then the larger
   single offcut). This is the only form of free-rectangle merging; general merging of
   non-sibling rectangles is not guillotine-valid and is not attempted.
3. **Bounded improvement** (`Options::improvementRuns`, default 150 extra runs): the piece
   order of the best run is perturbed (first: pieces of the emptiest sheet moved to the
   front to try to dissolve that sheet; then seeded pseudo-random swaps, moves and short
   reversals), keeping strict improvements; finally the emptiest sheet is re-packed on its
   own with a sample of the strategies.
4. The best run by the selected objective wins; ties keep the earlier run.

Objectives — lexicographic keys, smaller is better, always preceded by (unplaced area,
unplaced count). *Lost area* = sheet area − piece area − reusable remnant area
(= trim + kerf + waste offcuts).

| Objective | Key |
|---|---|
| `MinimizeSheets` | sheets, lost area, stock cost, cuts, cut length |
| `MinimizeWaste` | lost area, sheets, stock cost, cuts, cut length |
| `PreferRemnants` | full (non-remnant) sheets, their area, −(piece area cut from remnants), sheets, lost area; remnant stock is also opened first |
| `MinimizeCuts` | sheets, cuts, cut length, lost area (fewest cuts among layouts with the fewest sheets) |

**Determinism.** No hash-order or address dependence, own LCG with a fixed default seed,
tolerant comparisons: the same job and options give the same result on every platform —
unless `Options::timeBudgetMs` is set, which makes the result depend on machine speed.

**Cancellation.** `Options::isCancelled` is polled before every packing run. Cancelling
before the first run completes yields `ErrorCode::Cancelled`; later the best layout so far
is returned with `cancelled == true`.

### Lower bound — what is and is not proven

`sheetCountLowerBound` is the area bound: the smallest *k* such that the *k* largest
available usable areas cover the area of the placed pieces. If nothing is unplaced and the
layout uses exactly that many sheets, `isSheetCountProvenMinimal` is `true`: no layout can
use fewer sheets. This says nothing about waste, cuts or remnants, and the bound ignores
shapes and kerf, so `false` does **not** mean the sheet count can be improved.

## Validator

`validateLayout(job, result)` recomputes everything from the public data without using
the packer's bookkeeping: placements inside the usable area, sizes and rotation legality,
pairwise kerf gap, quantities, stock limits, a replay of the cut sequence (every cut must
hit an existing piece of material edge to edge, with the right kerf width and stage, and
the replay must leave exactly the placements and offcuts), offcut classification, all
areas, utilisations, totals, stock usage and the lower-bound flags. Every test of this
module runs it.

## Adapter

`jobsFromModel(model, settings, diagnostics)` creates one job per material: piece sizes
are `domain::cutSize()` (before edge banding), identical pieces are merged into
quantities, `canRotate = part.grain == None || !material.hasGrain`, sizes are swapped for
grain `AlongWidth`, stock is one unlimited sheet type of the material's sheet size. Parts
with a missing material, an invalid cut size or an invalid sheet size are reported in
`diagnostics`, never dropped silently.

## Limits

* Heuristic only; no exact or branch-and-bound solver, no optimality proof.
* Rectangles only, guillotine cuts only, 90° rotation only; one material per job.
* No head-cut/strip grouping, stack (book) cutting, saw-specific programs, minimum strip
  width for clamps, tension-relief cuts or label positions.
* `firstCut` is a preference, not a hard constraint; `maxStages` is a hard constraint and
  may cost sheets.
* Trim cuts are modelled without kerf geometry (the blade is assumed to run inside the
  trim strip); defects on sheets and non-rectangular remnants are not modelled.
* Offcut rectangles follow from the cut tree; the re-split step only swaps the two cuts
  around one piece, so a layout may contain adjacent offcuts that a person would keep as
  one larger remnant by cutting differently.
* Cost is only a tie-breaker; there is no cost-minimising objective.
* Single-threaded; typical kitchen job (150 pieces): well under a second.
