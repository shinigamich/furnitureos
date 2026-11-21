# fos::linear — 1D cutting-stock optimisation

Cuts pieces (rails, profiles, edge-band strips) from bars or rolls. Depends only on `fos::core`.
All lengths are millimetres along the bar.

| Header | Contents |
|---|---|
| `fos/linear/types.hpp` | `LinearJob`, `Piece`, `Stock`, `Objective`, `LinearResult`, `BarPlan`, `Cut`, `Unplaced` |
| `fos/linear/linear.hpp` | `validateJob()`, `usableLength()`, `optimizeLinear()`, `validateLinearPlan()` |
| `fos/linear/svg.hpp` | `renderLinearSvg()` — bars as horizontal strips, 1 unit = 1 mm |

## Bar semantics
* `trimStart` / `trimEnd` are removed at the bar ends (the trim includes its cut). Usable range
  `[trimStart, length - trimEnd]`.
* The first piece starts at `trimStart`; each further piece starts `kerf` after the previous end.
* No kerf at the bar end: a piece that exactly fills the remaining length fits
  (1998 + 4 + 1998 + 4 + 1996 = 6000 fits one 6000 bar with kerf 4; 3 x 1998 does not).
* If material remains after the last piece, one separating cut removes `min(kerf, remaining)`;
  the rest is the offcut, reusable when `>= minReusableOffcut > 0`, otherwise waste.

## Algorithm (HEURISTIC)
First-fit decreasing and best-fit decreasing, then `iterations` seeded perturbations of the
decreasing order (portable `mt19937_64` output, no library distributions), alternating first/best
fit. New bars open in objective order (priority, then: MinimizeBars longest first, MinimizeWaste
shortest first, PreferOffcuts offcuts first). Each candidate gets a bounded improvement (dissolve
the weakest bar into the others while possible) and a pass moving each bar to the shortest
suitable available stock. Candidates are compared by unplaced count, then the objective.
`barCountLowerBound = ceil(sum(length + kerf) / (max usable + kerf))`;
`isBarCountProvenMinimal` only when the bar count equals it. Above 5000 instances at most 4
perturbations run.

`validateLinearPlan()` recomputes positions, kerf gaps and counts, trims, offcuts, quantities,
stock availability, utilisation and totals without sharing code with the optimiser.

## Adapters
* `production::edgeBandStripJobs()` (`fos/production/edge_band_rolls.hpp`): one job per edge band,
  strips + overhang as pieces, unlimited rolls (default 100 m) as stock.
* CLI: `fos linear <pieces.csv> --stock ...`, `fos edgeband-rolls <project.fosp>`.

## Limitations
* No exact solver (no column generation / branch and bound); results are not optimal in general.
* One kerf and one trim pair per job (not per stock type); no angled (mitre) cuts.
* The lower bound ignores which pieces can share a bar, so it can be far below the optimum.
