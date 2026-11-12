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
      │      (expectations come from the domain operations, never from the generator)
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
| `toolpath.hpp` | `Motion` (Rapid, Linear, ToolChange, SpindleOn, SpindleOff, Dwell), `Program` |
| `generate.hpp` | `CamOptions`, `generateProgram` (drilling, pecking, grooves) |
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

Unsupported — always reported explicitly in `SetupPlan::unsupported`, never silently skipped:
* Every operation on an edge face (L1, L2, W1, W2): needs a horizontal spindle or aggregate.
* Pockets (blind and through): no pocket strategy exists that the simulator verifies.
* Holes larger than any drill (no helical/circular interpolation), arcs, canned cycles, contours, cut-outs,
  climb/conventional direction, lead-in ramps, clamps/fixtures, tool-length offsets, multiple parts per program.

## Safety model

1. `generateProgram` refuses parts with machining validation errors, invalid profiles/options,
   missing tools, tools above machine rpm/feed limits, safe Z below the clearance plane.
2. `simulate` checks independently: envelope, unknown start position, rapids below the clearance plane
   over the stock (pure upward retracts allowed), spoilboard penetration, cutting with no tool / spindle off,
   feed and rpm limits, flute length vs. depth, tool changes with spindle on or below safe Z, and a sparse
   heightmap (default 0.5 mm cells, flat-bottomed cylinder tool) verifying that every hole/groove is cut to its
   floor and that nothing is removed outside an operation volume (`cam.sim.gouge`) or below its floor
   (`cam.sim.too_deep`). It is coarse: features smaller than a cell are only warned about (`cam.sim.unresolved`),
   drill tips and tool deflection are ignored. Time is an estimate (no acceleration).
3. `Postprocessor::generate` always re-simulates and refuses when any error is reported. `PostOutput::verifiedForMachine`
   is `verified() && profile.verified`, i.e. always false here, and `reviewWarnings` lists dialect assumptions.
4. The output starts with a comment block stating the dialect is unverified and describing origin, face up,
   stock and tools; `checkRoundTrip` proves the text encodes exactly the simulated IR.

## Reference postprocessor

`generic-iso` — "Generic ISO G-code (RS-274 style), UNVERIFIED": `G21 G90 G17 G94`, `Tn M6`, `Sn M3`, `M5`,
`G0`/`G1` with explicit X Y Z (3 decimals) and F (1 decimal), `G4 P<seconds>` (warned: some controls read ms),
`M30`. No canned cycles, arcs, cutter compensation, work-offset or tool-length words.

## Adding a postprocessor

1. Derive from `Postprocessor`, implement `id()`, `name()` (say UNVERIFIED) and `emit()`; never override
   `verified()` to true unless the dialect has been proven on that control with documented evidence.
2. Emit only what the IR contains; missing data is an error, never a default. Start with an unverified header.
3. Add a parser for the emitted subset (or extend `roundtrip.hpp`) and golden/round-trip tests.
4. Register it in `builtinPostprocessors()` (no global registry).
