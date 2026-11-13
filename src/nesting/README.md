# nesting — true-shape nesting heuristic (CNC routing)

Module `fos::nesting` (deps: `core`, `geometry`). Places non-rectangular parts (simple polygons) on
rectangular stock sheets for routing. It is separate from `cutting` (guillotine saw plans) and
produces non-guillotine layouts. **It is a heuristic: results are never claimed optimal**
(`NestResult::isHeuristic` is always true).

## Model

* Sheet frame: origin at the sheet's lower-left corner, +X along the sheet length, +Y along its width.
  mm, mm², degrees (CCW positive).
* `Sheet`: length, width, quantity (`kUnlimited` = -1), per-side `Margins`. Sheet types are opened in
  the listed order (first type with stock left on which the part fits empty).
* `Part`: id, label, simple outline (either winding, first vertex not repeated), `holes` (data only:
  they reduce `totalNetPartArea`; no part is ever nested inside a hole), quantity, `RotationRule`
  (`None` = 0°, `Half` = 0/180° for grain, `Quarter` = 90° steps, `Step` = k·step for k·step < 360,
  step from the part or `NestOptions::rotationStepDeg`), `mirrorAllowed` (default false).
* Placement transform (shared exactly by optimiser and validator):
  `outline_sheet = translated(rotated(mirrored ? mirroredX(outline, 0) : outline, rotationDeg), translation)`
  i.e. mirror x → −x first, then CCW rotation about the part-frame origin, then translation.

## Spacing semantics

* `Job::partSpacing` = **tool diameter + extra clearance**. It is the minimum *Euclidean* distance
  between any two part outlines. One router pass between two parts removes a channel one tool
  diameter wide, so outlines must be at least that far apart; add clearance for deflection/tabs.
  0 means parts may touch (shared edges allowed, no interior overlap).
* `Job::edgeSpacing`: minimum distance from a part outline to the margin line. Parts must lie in the
  **placement area** = sheet inset by `margin + edgeSpacing` on each side (`placementArea()`).
  Containment in that rectangle is exact (all vertices inside, within 1e-6 mm).
* Valid iff for every pair on a sheet: `polygonDistance >= partSpacing − 1e-6` (spacing > 1e-6) or
  `!polygonsOverlap` (spacing 0), after an AABB-distance pre-filter.

## Algorithm

1. **Shapes.** For each part every allowed orientation (rotation × optional mirror) is built from the
   original outline; congruent orientations (same normalised ring) are dropped. Each orientation is
   split into convex pieces (ear-clipping triangulation + Hertel–Mehlhorn merging).
2. **Bottom-left-fill decoder** (one complete layout for a given instance order). For each instance,
   open sheets are tried first-fit in order (skipped when the free area is too small or the same part
   already failed there — sheets only fill up), then a new sheet is opened. For every orientation:
   * inner-fit rectangle (IFP) of reference positions (bbox min) inside the placement area;
   * for each placed part Q and each convex piece pair: NFP = Q_j ⊕ (−P_i) (convex Minkowski sum,
     exact for convex pieces), miter-inflated by `partSpacing` (`geometry::offsetPolygon`, a superset of
     the exact rounded offset, so positions on it keep ≥ spacing); cached per orientation pair;
   * candidates: IFP corners, NFP vertices, NFP edges ∩ IFP boundary, NFP edge ∩ NFP edge (bbox
     pre-filtered); clamped into the IFP;
   * **placement criterion**: minimise the right end of the part's bbox (x + width), then its bottom y;
     ties keep the earlier orientation (rotation ascending, unmirrored first). Candidates are scanned in
     (x, y) order; a candidate deeper than 1e-5 mm inside a raw NFP is rejected cheaply, every other one
     gets the **exact** collision test above; the first valid one per orientation is that orientation's
     best.
3. **Initial orders**: instances sorted by outline area, by longest bbox side and by bbox area
   (descending, ties by part order). The best layout by the key below is kept.
4. **Improvement loop** (`NestOptions::iterations`, a COUNT so results are reproducible): from the best
   order, a seeded move (swap two instances of different parts, or move one instance) produced by an
   own 32-bit LCG (`x = 1664525·x + 1013904223`); the layout is accepted when not worse.
   Comparison key: (unplaced instances, sheets used, used length of the last sheet).
5. **Result**: per sheet placements, used length (max X of outlines), remnant strip
   `[usedLength + partSpacing, length] × [0, width]` when its length is > 0 and ≥ `minRemnantLength`,
   part area, utilisation (outline area / sheet area). Unplaced instances carry `TooLarge` (no
   orientation's bbox fits any sheet type's placement area — the message lists the sizes per
   orientation and every placement area) or `StockExhausted`.
6. **Cancellation**: `isCancelled` is polled per instance and orientation. Before the first complete
   layout → `ErrorCode::Cancelled`; later → best layout so far with `cancelled = true`.

`validateNest(job, result)` re-derives every outline from the original polygon and checks containment,
spacing, rotations, mirroring, instances/quantities, stock counts, areas, utilisation, used length and
remnant. It never trusts the optimiser. `renderNestSvg()` draws one sheet (1 unit = 1 mm, y flipped,
margins, placement area, remnant, XML-escaped labels).

## Complexity

Per placement and orientation with k relevant NFPs of ≤ e edges: candidate generation O(k·e +
Σ overlapping pairs · e²), candidate test O(k) for the NFP reject plus O(n·m) per neighbour for
`polygonDistance` (O(n·m·(n+m)) for `polygonsOverlap` at spacing 0). A decode is roughly quadratic in
the parts per sheet. Measured: the 60-part mixed test job (7 shapes, 3 sheets) with 10 iterations
runs in ~1.3–1.8 s in an unoptimised build.

## Limitations

* Heuristic only; no lower-bound proof beyond the area bound mentioned in tests.
* No nesting inside holes; holes are data/area only.
* Rotations are a finite set; the arbitrary-angle mode is a step grid, not continuous rotation.
* Candidate positions are a finite set (NFP/IFP vertices and edge intersections); positions touching
  three parts along arc-shaped spacing boundaries (rounded corners) can be missed because the miter
  offset is used for candidate generation (the exact distance test is still authoritative).
* No part-in-part priority, no common-line cutting, no lead-ins/tabs, no grain beyond the rotation rule,
  no multi-threading. Remnant is only the full-width strip right of the used length.
* At most 10000 instances and 1000 outline vertices per part (validateJob).
