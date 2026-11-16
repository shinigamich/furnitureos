<!-- SPDX-License-Identifier: AGPL-3.0-only -->
# `cam` — machine-independent CAM foundation (UNVERIFIED)

> **No machine is verified.** Nothing in this module has been run on, or proven against, any
> real machine or controller. Every profile and every output is labelled unverified. Output
> must be reviewed and proven by a qualified operator on the actual control (dry run above the
> stock, hand on the emergency stop) before any use. FurnitureOS never sends anything to a machine.

Depends on `core`, `geometry`, `domain`, `machining`.

## Architecture

```
domain::Part (operations)            machining::validateOperations / orderOperations
      │
      ▼
planSetup (setup.hpp) ── partition: machinable now / needs other setup / unsupported
      │
      ▼
generateProgram (generate.hpp) ── Program: ordered Motion IR, absolute machine coordinates
      │
      ▼
simulate (simulate.hpp) ── independent checks + heightmap material verification
      │      (expectations come from the domain operations, never from the generator's motions;
      │       the face mapping in setup.cpp is shared — see Safety model)
      ▼
Postprocessor::generate (postprocessor.hpp) ── re-runs simulate(), REFUSES on any error
      │
      ▼
GenericIsoPostprocessor ── text  ──►  parseGenericIso / checkRoundTrip (roundtrip.hpp)
```

| Header | Contents |
|---|---|
| `tool.hpp` | `Tool`, `ToolKind {Drill, EndMill}`, `ToolLibrary` (validation, `selectDrill`, `selectGrooveCutter`) |
| `machine.hpp` | `MachineProfile` (envelope, safe Z, clearance, limits, `verified = false`), `validateProfile` |
| `setup.hpp` | `Setup`, `SetupFrame`, frame mapping, `planSetup`, `HoleTarget`/`GrooveTarget` |
| `toolpath.hpp` | `Motion` (Rapid, Linear, ArcCW, ArcCCW with exact centre, ToolChange, SpindleOn, SpindleOff, Dwell), `Program` |
| `contour.hpp` | `ContourTarget`, `needsContourRouting`, `contourToMachineXY`, `contourTargets` (shaped parts) |
| `generate.hpp` | `CamOptions`, `ContourDirection`, `generateProgram` (drilling, pecking, grooves, contour routing) |
| `simulate.hpp` | `simulate`, `SimulationReport` (diagnostics, path lengths, time estimate) |
| `postprocessor.hpp` | `Postprocessor` (NVI: `generate` simulates then `emit`s), `PostprocessorRegistry` |
| `generic_iso.hpp` | the single reference postprocessor |
| `roundtrip.hpp` | parser for the emitted subset and IR comparison |

## Frames

Machine frame: right-handed, X/Y on the table, Z up, mm; origin = work zero set on the control
(Z = 0 normally the spoilboard top). `Setup::stockOrigin` = machine position of the clamped
stock's min corner (the fixture offset). The part's length always runs along machine +X.

| Face up | part (x, y, z) → machine | face point (u, v) → machine XY | surface Z |
|---|---|---|---|
| A | o + (x, y, z) | (o.x + u, o.y + v) | o.z + T |
| B | o + (x, W − y, T − z) | (o.x + u, o.y + W − v) | o.z + T |

Face B up means the panel is turned over about its **length axis** (180° about local X, a proper
rotation). The face-B mapping is computed with `machining::flippedFaceCoordinates(…, FlipAxis::Length)`;
tests check it against an independently written rotation and against the physical spot
(the left side's front pin stays 37 mm from the front edge L1, which is now at max Y).

## Supported / unsupported

Supported (3-axis, single vertical spindle, tool along −Z):
* Holes on the upward face with a drill of exactly the hole diameter; blind holes to depth,
  through holes to stock bottom − breakthrough allowance (≤ profile spoilboard limit); optional pecking
  (retract to the clearance plane, *feed* back down), optional dwell.
* Straight grooves on the upward face with a centre-cutting end mill ≤ groove width: step-down by
  `maxDepthOfCut`, several lateral passes when the groove is wider than the tool, tool-centre offsets
  computed in the IR (no G41/G42). Open ends (axis-parallel groove ending on the panel edge) are run out
  past the edge; closed ends are rounded with the tool radius (warning `cam.groove.rounded_end`).

* Contour routing of shaped parts (ADR-0008; `needsContourRouting(part)` = `!domain::isRectangularOutline`),
  see below.

Unsupported — always reported explicitly in `SetupPlan::unsupported`, never silently skipped:
* Every operation on an edge face (L1, L2, W1, W2): needs a horizontal spindle or aggregate.
* Pockets (blind and through): no pocket strategy exists that the simulator verifies.
* Holes larger than any drill (no helical interpolation of holes), canned cycles, tabs/bridges, ramped
  entries, clamps/fixtures, tool-length offsets, multiple parts per program.

## Contour routing (phase 12)

* **Source.** No new domain operation type: the targets come straight from the domain
  (`contourTargets`): every cut-out of `PartShape` (as given; cut-outs are never banded) and the
  **raw** outline `domain::cutContour(part, model)` — the panel is routed before edge banding. Synthetic
  ids `contour.cutout.<k>` and `contour.outline` (a part operation with such an id is refused).
  Edge bands come from `CamOptions::model` / `SimulationOptions::model` (required when any outline
  segment is banded). Routing runs after holes and grooves: cut-outs first, then the outline.
* **Blank.** `Setup::blankAllowance` (shaped parts only) makes the clamped stock the part bounding box
  grown by that much per side; `stockOrigin` is the blank's min corner, the part origin is
  `stockOrigin + (a, a, 0)`. With 0 the stock is the bounding box and straight raw edges on it cut air.
* **Compensation in the IR.** Tool-centre path = `geometry::offsetContour(raw, +r)` (outline, tool
  outside) / `offsetContour(cut-out, −r)` (inside). Arcs stay concentric arcs, emitted as exact
  `ArcCW/ArcCCW` (R50 outline corner, 8 mm cutter → R54; R50 cut-out corner → R46). Sharp corners whose
  waste side is a wedge stay rounded with the tool radius (`cam.contour.rounded_corner`).
* **Tool.** `CamOptions::contourToolNumber`, or the largest centre-cutting end mill whose flute covers
  T + breakthrough and for which every offset exists.
* **Direction.** Spindle CW (M3). `Climb` (default): part on the right of the tool — outline clockwise,
  cut-outs counter-clockwise (seen from +Z). `Conventional`: reversed.
* **Depth.** Step-downs of `maxDepthOfCut` to stock bottom − `breakthroughAllowance` (≤ spoilboard limit).
* **Lead-in/out.** Tangential quarter-circle **arcs** (radius `contourLeadRadius`, default tool radius) on
  the waste side at the midpoint of the longest straight path segment; plunge at plunge rate at the
  lead-in start (in the waste), loop, lead-out, retract — per level. No tabs.
* **Simulation.** Arcs are rasterised exactly (covered angular window per cell). Expected: on each
  contour's waste side, every cell within r of the contour and reachable (within r of the ideal offset
  path) is cut through to the stock bottom (`cam.sim.contour_not_cut`); waste may be cut down to the
  spoilboard limit; anything removed inside the finished part area beyond `contourTolerance` (0.001 mm)
  and outside holes/grooves is `cam.sim.gouge`. `cam.sim.arc_invalid` for inconsistent radii/full circles.
  The raw outline comes from the domain; the offset used for the reach test is computed by the
  simulator, not taken from the program. The face-B mapping of contours (`contourToMachineXY`) is shared
  with the generator and tested separately from first principles.

## Safety model

1. `generateProgram` refuses parts with machining validation errors, invalid profiles/options,
   missing tools, tools above machine rpm/feed limits, safe Z below the clearance plane, a stock origin
   below Z = 0 (`makeSetupFrame`: Z = 0 is the spoilboard top), and any generated position outside the
   machine envelope (e.g. a groove lead-out past an open end with the stock clamped near the envelope
   origin) — with a message naming the operation, the position and the envelope.
2. `simulate` checks: safe Z against this stock's clearance plane (top + clearance; `validateProfile` cannot,
   it does not know the stock), envelope, unknown start position, rapids below the clearance plane
   over the stock (pure upward retracts allowed; a move from an UNKNOWN X/Y counts as sweeping the whole
   table, so it must be at or above the clearance plane), spoilboard penetration, cutting with no tool / spindle off,
   feed and rpm limits, flute length vs. depth, tool changes with spindle on or below safe Z, and a sparse
   heightmap (default 0.5 mm cells over the whole stock, the last row/column clipped to the stock edge;
   flat-bottomed cylinder tool; tools thinner than 2 cells are refused with `cam.sim.tool_below_resolution`
   because their cuts could miss every cell centre) verifying that every hole/groove is cut to its
   floor and that nothing is removed outside an operation volume (`cam.sim.gouge`) or below its floor
   (`cam.sim.too_deep`). It is coarse: features smaller than a cell are only warned about (`cam.sim.unresolved`),
   drill tips and tool deflection are ignored. Time is an estimate (no acceleration).
   **Not independent on face mapping:** the simulator derives its expected hole/groove volumes with the
   same setup mapping as the generator (`holeTarget`, `grooveTarget`, `faceToMachineXY` in `setup.cpp`).
   A wrong face-B flip would move generator and expectations together and pass the simulation. The mapping
   is therefore tested separately against first principles: `CamHardening.FaceBHoleMachineXYFromFirstPrinciples`
   recomputes the machine XY of every face-B hole of the left side from the domain face definition
   (B = z 0 plane, u = x, v = y) and a rotation of π about the length axis, without calling `setup.cpp`.
3. `Postprocessor::generate` always re-simulates and refuses when any error is reported. `PostOutput::verifiedForMachine`
   is `verified() && profile.verified`, i.e. always false here, and `reviewWarnings` lists dialect assumptions.
4. The output starts with a comment block stating the dialect is unverified and describing origin, face up,
   stock and tools; `checkRoundTrip` proves the text encodes exactly the simulated IR.

## Reference postprocessor

`generic-iso` — "Generic ISO G-code (RS-274 style), UNVERIFIED": `G21 G90 G17 G94`, `Tn M6`, `Sn M3`, `M5`,
`G0`/`G1` with explicit X Y Z (3 decimals) and F (1 decimal), `G2`/`G3` X Y Z I J F (I/J = centre
**incremental from the arc start**, computed from the rounded centre and start so the printed centre is
exact to 0.001 mm; review warnings about G90.1 controls and arc end-point tolerance), `G4 P<seconds>`
(warned: some controls read ms), `M30`. No canned cycles, R-format arcs, cutter compensation, work-offset
or tool-length words. `parseGenericIso`/`checkRoundTrip` read G2/G3 back and compare end points and centres.

## Adding a postprocessor

1. Derive from `Postprocessor`, implement `id()`, `name()` (say UNVERIFIED) and `emit()`; never override
   `verified()` to true unless the dialect has been proven on that control with documented evidence.
2. Emit only what the IR contains; missing data is an error, never a default. Start with an unverified header.
3. Add a parser for the emitted subset (or extend `roundtrip.hpp`) and golden/round-trip tests.
4. Register it in `builtinPostprocessors()` (no global registry).
