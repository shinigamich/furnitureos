# Cutting and nesting algorithms: short survey

Date: 2026-11-11. Algorithm descriptions are general knowledge; licences marked "verified" were read on the project's GitHub page in this session, others are not verified.

## 1. Rectangular cutting (panels on sheets)

Panel saws make **guillotine** cuts: every cut runs edge to edge across the current piece. Many shops also require 2-stage or 3-stage patterns and a minimum trim. Kerf (blade width) and trim must be modelled. Grain restricts rotation.

| Method | Guillotine? | Notes |
|---|---|---|
| Shelf heuristics (NFDH, FFDH, BFDH) | Yes (2-stage) | Simple, fast; sort by height first; waste is moderate. |
| Guillotine free-rectangle heuristics (e.g. Best Area/Short Side Fit with split rules: shorter/longer axis, min/max area) | Yes | Place into a free rectangle, split the remainder with a guillotine split; many placement/split rule combinations, and trying several and keeping the best is cheap. Recommended baseline. |
| MaxRects | **No** (non-guillotine) | Produces better packing density but layouts cannot be cut on a panel saw; suitable for routers/nesting only. |
| Column generation / LP relaxation (Gilmore-Gomory) | Possible with staged patterns | Solves the cutting-stock problem for many identical items; gives lower bounds; needs an LP solver (libraries and their licences to be chosen). |
| Exact methods (ILP, branch-and-price, dynamic programming for 2-stage patterns) | Yes | Optimal only for small instances or restricted staging; time limits required. |
| Metaheuristics (genetic algorithms, simulated annealing, local search over item order) | Via a guillotine decoder | Improve heuristics given time budget; nondeterminism must be controlled by a seed. |

Validation: any layout must be checked independently (no overlap, inside the sheet, guillotine-feasibility, kerf included) by a validator that does not trust the optimiser.

## 2. True-shape nesting

| Technique | Notes |
|---|---|
| No-fit polygon (NFP) | Exact set of relative positions where two polygons touch without overlapping; computing NFPs for non-convex shapes is the hard part; used with bottom-left-fill or local search. |
| Bottom-left-fill (BLF) over a given order | Simple constructive placement; quality depends on item order and rotation set. |
| Raster / pixel methods | Robust and simple, accuracy limited by resolution; can be a first filter. |
| Search over order and rotation (GA, SA) | Common in practice on top of NFP/BLF. |

Libraries and licences:
* **SVGnest**: MIT (verified on GitHub). Browser JavaScript; uses Clipper (the JavaScript port) and genetic search; algorithmic reference, not a drop-in C++ component.
* **Deepnest**: derived from SVGnest; the GitHub page opened in this session (a repository under the Jack000 account) showed no licence file, so licence for that repository is **not established**; other forks may differ. Verify the fork and its licence before using code or ideas closely tied to it.
* **libnest2d**: LGPL-3.0 (verified on GitHub), from the PrusaSlicer lineage; README says it works well for rectangles and convex polygons without holes/concavities and is not feature complete. LGPL adds relinking obligations; its fitness for furniture contours with holes is doubtful.
* **Clipper2**: BSL-1.0 (verified); polygon offset and boolean operations, a building block for NFP-free approaches, kerf offsetting and overlap tests. The original repository is shown as archived with maintenance moving to `PolyClipping/Clipper2` (verified); confirm before adoption.

## 3. Recommendation for FurnitureOS

1. `cutting` (rectangular, guillotine): implement shelf and guillotine free-rectangle heuristics with several split/ordering rules, pick the best, optionally improved by a time-bounded randomised search with an explicit seed. State clearly in UI and docs that these are **heuristics, not optimal**. Add an independent layout validator (including kerf and grain).
2. Do **not** use MaxRects for panel-saw plans; allow it only for router nesting where non-guillotine cuts are acceptable.
3. `nesting` (polygons): start with BLF over ordered items and a restricted rotation set (grain permitting), overlap test through polygon intersection, offset by tool radius/kerf with in-house code or Clipper2 (ADR-0002). Add NFP only if measured packing density demands it.
4. Do not adopt libnest2d or take code from repositories with unclear licences. Algorithmic ideas from SVGnest are usable; code reuse requires MIT notice compliance.
5. Exact methods/column generation: later and optional; need solver selection and licence review.
6. Benchmark on public instance sets (not selected or tested here) and publish results honestly.
