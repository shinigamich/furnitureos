<!-- SPDX-License-Identifier: AGPL-3.0-only -->
# `cli` — the `fos` command-line tool

Module `fos::cli_core` (library with every command, testable in-process: `fos::cli::run(args, out, err)`
in `fos/cli/cli.hpp`) and the executable `fos` (a thin `main()`). Depends on `project` and the engineering
modules below it (`validation`, `production`, `cutting`, `drawings`, `cam`, `formats`, …). Offline, no network,
English messages, deterministic output for the same file and arguments (only `fos new` without `--id`/`--date`
draws a UUID and reads the clock, and saves stamp `savedAt`). Lengths are millimetres.

| Exit code | Meaning |
|---|---|
| 0 | success (warnings may have been printed) |
| 1 | the command ran and found errors: validation errors, a refused parameter, a refused CAM program, a drawing that does not fit at the requested scale, a cutting layout with validator errors |
| 2 | usage or I/O error: unknown command/option, missing argument, unknown item/part id, unreadable/corrupt project file, unwritable output |

Every command prints its usage with `--help`; `fos --help` lists the commands. `fos nest` does not exist:
projects contain no polygon (true-shape) parts yet.

## Commands

All output below is real output of this build, captured on the reference project
(`fos new … --template base --width 800 --height 720`, then widened to 900 by `fos set`). Very long lines are
shown in full; `…` marks lines removed from long listings.

### `fos new`

Creates a `.fosp` project with the generic placeholder libraries (no manufacturer, cost 0 = unknown). `--template base|wall|tall|drawers|wardrobe|shelving` adds one cabinet (item id `cab1`, or `--item-id`); `--width/--height/--depth` are applied through the same undoable document commands the desktop application uses. `--id`/`--date` make the file reproducible (default: random UUID v4, current UTC time). An existing file is never overwritten without `--force`.

```text
$ fos new kitchen.fosp --name Kitchen --author "J. Doe" --template base --width 800 --height 720 --id 0b6f6a52-6a4e-4c8e-9a3b-6f1d2c3e4f50 --date 2026-11-13T09:00:00Z
Created kitchen.fosp (project 'Kitchen', id 0b6f6a52-6a4e-4c8e-9a3b-6f1d2c3e4f50)
  item cab1: Base cabinet, W 800 x H 720 x D 560 mm, 10 parts
(exit code 0)
```

### `fos info`

Metadata, libraries, every item with its envelope, input parameters (`--all`: also every formula with its value), part counts and generator diagnostics. Exit 1 when the diagnostics contain errors.

```text
$ fos info kitchen.fosp
Project: Kitchen
  id: 0b6f6a52-6a4e-4c8e-9a3b-6f1d2c3e4f50
  author: J. Doe
  file format version: 1
  libraries: 3 materials, 2 edge bands, 7 hardware definitions
Items: 1
  cab1  cabinet  "Base cabinet"  W 800 x H 720 x D 560 mm  parts: 10
    parameters: 53 (28 inputs, 25 formulas)
      back_inset = 2
      back_thickness = 3
      depth = 560
      front_gap = 3
      front_offset = 2
…
      width = 800
Parts: 10 in 1 generated assemblies
Diagnostics: none
(exit code 0)
```

### `fos set`

`fos set <file> <item-id> <parameter> <value-or-expression>` — a plain number sets a constant, anything else is stored as an expression (`fos set k.fosp cab1 depth "height / 2"`). Runs `project::commands::setParameter[Expression]` through `project::Document` and saves atomically (temp file, verify, rotate `.bak1..3`, rename). A refused value saves nothing (exit 1). A value the document accepts but that leaves the model with errors (e.g. a pinned `bottom_length` that makes the bottom penetrate a side) is saved, the errors are listed, the previous value is printed and the exit code is 1.

```text
$ fos set kitchen.fosp cab1 width 900
width of cab1: 800 -> 900
Saved kitchen.fosp
(exit code 0)
```

```text
$ fos set kitchen.fosp cab1 width 20
fos set: refused: Constraint 'std.inner_width' violated: Inner width must be positive: width 20 mm minus two 18 mm sides leaves -16 mm (inner_width > 0 is false; inner_width = -16).
Nothing was saved.
(exit code 1)
```

### `fos validate`

Generator self-check diagnostics followed by `validation::validateModel` (`--format text|markdown`). Exit 1 when any error is found.

```text
$ fos validate kitchen.fosp
Generator diagnostics: none
Validation passed: 0 error(s), 0 warning(s), 6 info(s); 10 part(s) in 1 assembly(ies), 7 check(s) run.
[info] validation.completeness.unknown_cost (mat.generic.hdf3): Material 'mat.generic.hdf3' has no cost per sheet; cost reports will exclude it.
[info] validation.completeness.unknown_cost (mat.generic.pb18): Material 'mat.generic.pb18' has no cost per sheet; cost reports will exclude it.
[info] validation.hardware.unverified_dimensions (hw.handle_bar_128): Hardware 'Bar handle, 128 mm hole spacing (generic)' (hw.handle_bar_128, used by 2 instance(s)) has dimensions that were not verified against a manufacturer data sheet; check the drilling before production.
[info] validation.hardware.unverified_dimensions (hw.confirmat_7x50): Hardware 'Confirmat screw 7x50 (generic)' (hw.confirmat_7x50, used by 8 instance(s)) has dimensions that were not verified against a manufacturer data sheet; check the drilling before production.
[info] validation.hardware.unverified_dimensions (hw.hinge_cup_35): Hardware 'Cup hinge 35 mm with mounting plate (generic)' (hw.hinge_cup_35, used by 2 instance(s)) has dimensions that were not verified against a manufacturer data sheet; check the drilling before production.
[info] validation.hardware.unverified_dimensions (hw.shelf_pin_5): Hardware 'Shelf support pin 5 mm (generic)' (hw.shelf_pin_5, used by 1 instance(s)) has dimensions that were not verified against a manufacturer data sheet; check the drilling before production.
(exit code 0)
```

### `fos report`

`--type parts|cutlist|materials|edgebanding|hardware|machining|summary` prints a text table followed by the report's diagnostics; `--csv out.csv [--separator ';']` writes RFC 4180 CSV instead; `--html out.html <item-id>` writes the printable production sheet of one item. Cut sizes are saw sizes (finished size minus edge banding, ADR-0004). Sheet counts in the materials report are *area lower bounds*.

```text
$ fos report kitchen.fosp --type cutlist
Material                       Thickness  Cut length  Cut width  Qty  Finished length  Finished width  Grain  Rotation allowed  Edge L1                  Edge L2                  Edge W1                    Edge W2                    Name                Operations  Part numbers  Part IDs
-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
HDF 3 mm (generic)             3          896         616        1    896              616             none   yes                                                                                                                       Back panel          0           10            cab1/back
Particleboard 18 mm (generic)  18         864         558        1    864              560             none   yes               ABS 2 x 22 mm (generic)                                                                                 Bottom              6           3             cab1/bottom
Particleboard 18 mm (generic)  18         864         100        2    864              100             none   yes                                                                                                                       Rear rail / Plinth  4           5, 6          cab1/rail.back, cab1/plinth
Particleboard 18 mm (generic)  18         864         98         1    864              100             none   yes               ABS 2 x 22 mm (generic)                                                                                 Front rail          4           4             cab1/rail.front
Particleboard 18 mm (generic)  18         862         538        1    862              540             none   yes               ABS 2 x 22 mm (generic)                                                                                 Shelf               0           9             cab1/shelf.1
Particleboard 18 mm (generic)  18         719.2       558        1    720              560             none   yes               ABS 2 x 22 mm (generic)                           ABS 0.4 x 22 mm (generic)  ABS 0.4 x 22 mm (generic)  Left side           15          1             cab1/side.left
Particleboard 18 mm (generic)  18         719.2       558        1    720              560             none   yes               ABS 2 x 22 mm (generic)                           ABS 0.4 x 22 mm (generic)  ABS 0.4 x 22 mm (generic)  Right side          15          2             cab1/side.right
Particleboard 18 mm (generic)  18         612         442.5      1    616              446.5           none   yes               ABS 2 x 22 mm (generic)  ABS 2 x 22 mm (generic)  ABS 2 x 22 mm (generic)    ABS 2 x 22 mm (generic)    Left door           4           7             cab1/door.left
Particleboard 18 mm (generic)  18         612         442.5      1    616              446.5           none   yes               ABS 2 x 22 mm (generic)  ABS 2 x 22 mm (generic)  ABS 2 x 22 mm (generic)    ABS 2 x 22 mm (generic)    Right door          4           8             cab1/door.right
(exit code 0)
```

```text
$ fos report kitchen.fosp --type summary
Assembly      Parts  Hardware items  Operations  Finished area (m2)  Width  Depth  Height
-----------------------------------------------------------------------------------------
Base cabinet  10     28              56          3.1169              900    583    720
Total: 10 parts, 28 hardware items, 56 operations, finished area 3.1169 m2, cut area 3.0995 m2, edge banding 10.52 m
(exit code 0)
```

```text
$ fos report kitchen.fosp --type cutlist --csv cutlist.csv --separator ';'
Wrote cutlist report (9 rows) to cutlist.csv
(exit code 0)
```

```text
$ fos report kitchen.fosp --html cab1.html cab1
Wrote production sheet of cab1 to cab1.html
(exit code 0)
```

### `fos cut`

One cutting job per board material (`project::cuttingPlans`: saw sizes, project kerf/trim/remnants), the guillotine optimiser (a **heuristic**) and the independent `cutting::validateLayout`. Writes `<dir>/<material>-sheet-<n>.svg` (1 user unit = 1 mm) and `<dir>/cutting-summary.txt` (the text below). Exit 1 when the validator or optimiser reports an error; unplaced pieces are listed and reported as warnings.

```text
$ fos cut kitchen.fosp --out-dir cutting
Cutting plans of 'Kitchen'
Layouts are produced by a HEURISTIC optimiser; utilisation is that of the layout found, which is in general not optimal.

Material mat.generic.pb18 "Particleboard 18 mm (generic)" (18 mm)
  kerf 4 mm, trim left/right/bottom/top 10/10/10/10 mm
  pieces: 9 required, 9 placed, 0 unplaced
  sheet 1: mat.generic.pb18/sheet 2800 x 2070 mm, 9 pieces, utilisation 43.95 % of the sheet (44.7 % of the usable area), 17 cuts -> mat.generic.pb18-sheet-1.svg
  total: 1 sheet(s), placed area 2.5476 m2 of 5.796 m2 sheet area = utilisation 43.95 % (heuristic result)
  sheet count lower bound (area): 1 - sheet count proven minimal
  strategy: heuristic: sort=longer-side, split=longer-leftover-axis, fit=best-area-fit, orientation=best-of-both, stock=largest-first
  validator (cutting::validateLayout): no findings

Material mat.generic.hdf3 "HDF 3 mm (generic)" (3 mm)
  kerf 4 mm, trim left/right/bottom/top 10/10/10/10 mm
  pieces: 1 required, 1 placed, 0 unplaced
  sheet 1: mat.generic.hdf3/sheet 2800 x 2070 mm, 1 pieces, utilisation 9.52 % of the sheet (9.68 % of the usable area), 6 cuts -> mat.generic.hdf3-sheet-1.svg
  total: 1 sheet(s), placed area 0.5519 m2 of 5.796 m2 sheet area = utilisation 9.52 % (heuristic result)
  sheet count lower bound (area): 1 - sheet count proven minimal
  strategy: heuristic: sort=area, split=shorter-leftover-axis, fit=best-area-fit, orientation=best-of-both, stock=largest-first
  validator (cutting::validateLayout): no findings

Diagnostics: none
Wrote 2 sheet SVG(s) and cutting-summary.txt to cutting
(exit code 0)
```

### `fos drawing`

An item id gives the assembly drawing (front view, top view below, left view to the right — ISO first angle — with automatic dimensions and the project's stored drawing edits); a part id gives the part drawing. `--svg` writes the sheet at exact scale (`width="420mm" height="297mm" viewBox="0 0 420 297"`, 100 mm calibration line); `--dxf` writes the first view in model space at 1:1 mm (DXF R12). `--paper A4|A3|A2|A1` (landscape; `--portrait`), `--scale fit|1:N`. An explicit scale that does not fit is an error, never silently changed. Stale text overrides are reported as warnings.

```text
$ fos drawing kitchen.fosp cab1 --svg cab1.svg --dxf cab1-front.dxf --paper A3 --scale fit
Wrote cab1.svg: A3 landscape 420 x 297 mm, scale 1:10, 3 view(s)
Wrote cab1-front.dxf: view cab1/view.front at 1:1 (mm, DXF R12)
(exit code 0)
```

```text
$ fos drawing kitchen.fosp cab1 --svg big.svg --paper A4 --scale 1:1
fos drawing: At scale 1:1 the views of 'Base cabinet' with dimensions need 1525.9 x 1354.2 mm, but the drawing area of the 297 x 210 mm sheet is only 267 x 138 mm. Choose a smaller scale, a larger sheet or fit-to-page.
(exit code 1)
```

### `fos labels`

One label per part instance (70 × 37 mm, 3 × 8 per A4 page) as `DIR/labels-<n>.svg`.

```text
$ fos labels kitchen.fosp --svg-dir labels
Wrote 10 labels on 1 page(s) to labels
(exit code 0)
```

### `fos cam`

UNVERIFIED 3-axis program for the operations on face A or B of one part, using the FICTIONAL **"FurnitureOS demo profile — not a real machine"** and a fictional tool library (`fos/cli/demo_cam.hpp`: drills 3 4 5 7 8 10 12 15 20 35 mm, end mills 4 6 8 mm; stock min corner at machine (20, 20, 0)). The program is simulated independently and the generic ISO postprocessor refuses output when the simulation reports any error (exit 1). Operations on the other face are listed as needing the other setup, edge operations and pockets as UNSUPPORTED — never silently dropped. `verifiedForMachine` is always false.

```text
$ fos cam kitchen.fosp cab1/side.left --face B --out side-left-B.nc
Part cab1/side.left "Left side" 720 x 560 x 18 mm, 15 operations, face B up
Machine: FurnitureOS demo profile — not a real machine (fictional, verified: no)
Machining validation: no findings
Machinable in this setup: 6
  cab1/hw.hinges.door.left/op.2
  cab1/hw.hinges.door.left/op.3
  cab1/hw.hinges.door.left/op.5
  cab1/hw.hinges.door.left/op.6
  cab1/hw.pins.shelf.1/op.1
  cab1/hw.pins.shelf.1/op.2
Needs the other face up: 9
  cab1/hw.joint.bottom.side.left/op.1: Operation 'cab1/hw.joint.bottom.side.left/op.1' on face A of 'Left side' needs a setup with face A up (current setup: face B up).
  cab1/hw.joint.bottom.side.left/op.3: Operation 'cab1/hw.joint.bottom.side.left/op.3' on face A of 'Left side' needs a setup with face A up (current setup: face B up).
  cab1/hw.joint.bottom.side.left/op.5: Operation 'cab1/hw.joint.bottom.side.left/op.5' on face A of 'Left side' needs a setup with face A up (current setup: face B up).
  cab1/hw.joint.rail.front.side.left/op.1: Operation 'cab1/hw.joint.rail.front.side.left/op.1' on face A of 'Left side' needs a setup with face A up (current setup: face B up).
  cab1/hw.joint.rail.front.side.left/op.3: Operation 'cab1/hw.joint.rail.front.side.left/op.3' on face A of 'Left side' needs a setup with face A up (current setup: face B up).
  cab1/hw.joint.rail.back.side.left/op.1: Operation 'cab1/hw.joint.rail.back.side.left/op.1' on face A of 'Left side' needs a setup with face A up (current setup: face B up).
  cab1/hw.joint.rail.back.side.left/op.3: Operation 'cab1/hw.joint.rail.back.side.left/op.3' on face A of 'Left side' needs a setup with face A up (current setup: face B up).
  cab1/hw.joint.plinth.side.left/op.1: Operation 'cab1/hw.joint.plinth.side.left/op.1' on face A of 'Left side' needs a setup with face A up (current setup: face B up).
  cab1/hw.joint.plinth.side.left/op.3: Operation 'cab1/hw.joint.plinth.side.left/op.3' on face A of 'Left side' needs a setup with face A up (current setup: face B up).
UNSUPPORTED (not in this program): 0
CAM diagnostics:
  [warning] cam.profile.unverified (FurnitureOS demo profile — not a real machine): Machine profile 'FurnitureOS demo profile — not a real machine' (controller: 'fictional controller (demo profile — not a real machine)') is NOT verified: Demo profile — not a real machine. Not verified against any physical machine or controller.
Simulation: 0 errors, 0 warnings; 1 tool change(s), rapid 1164 mm, cutting 108 mm, estimated 15 s (estimate, no acceleration)
Wrote side-left-B.nc (Generic ISO G-code (RS-274 style), UNVERIFIED)
verifiedForMachine: false - UNVERIFIED output for a fictional demo machine; review before any use.
Review before use:
  - Generic dialect: not verified against any controller; prove it on the actual control first.
  - No work offset (G54..G59) is selected: set work zero on the control so that it matches the profile frame.
  - No tool length offset word is emitted: confirm the control applies tool length compensation after M6.
  - M6 tool change behaviour (automatic changer, manual prompt, or ignored) depends on the control.
  - M3 assumes clockwise spindle rotation; confirm it matches the tools.
  - No spindle spin-up wait is guaranteed: confirm the control waits for speed after M3.
  - Fixtures and clamps are not modelled: check that no clamp lies under any toolpath.
  - Machine profile 'FurnitureOS demo profile — not a real machine' is not verified: Demo profile — not a real machine. Not verified against any physical machine or controller.
(exit code 0)
```

```text
$ fos cam kitchen.fosp cab1/bottom --face A --out bottom-A.nc
Part cab1/bottom "Bottom" 864 x 560 x 18 mm, 6 operations, face A up
Machine: FurnitureOS demo profile — not a real machine (fictional, verified: no)
Machining validation: no findings
Machinable in this setup: 0
Needs the other face up: 0
UNSUPPORTED (not in this program): 6
  cab1/hw.joint.bottom.side.left/op.2: Operation 'cab1/hw.joint.bottom.side.left/op.2' on face W1 of 'Bottom' is on an edge face; it needs a horizontal spindle or an angle aggregate, which a 3-axis vertical-spindle machine profile does not have.
  cab1/hw.joint.bottom.side.left/op.4: Operation 'cab1/hw.joint.bottom.side.left/op.4' on face W1 of 'Bottom' is on an edge face; it needs a horizontal spindle or an angle aggregate, which a 3-axis vertical-spindle machine profile does not have.
  cab1/hw.joint.bottom.side.left/op.6: Operation 'cab1/hw.joint.bottom.side.left/op.6' on face W1 of 'Bottom' is on an edge face; it needs a horizontal spindle or an angle aggregate, which a 3-axis vertical-spindle machine profile does not have.
  cab1/hw.joint.bottom.side.right/op.2: Operation 'cab1/hw.joint.bottom.side.right/op.2' on face W2 of 'Bottom' is on an edge face; it needs a horizontal spindle or an angle aggregate, which a 3-axis vertical-spindle machine profile does not have.
  cab1/hw.joint.bottom.side.right/op.4: Operation 'cab1/hw.joint.bottom.side.right/op.4' on face W2 of 'Bottom' is on an edge face; it needs a horizontal spindle or an angle aggregate, which a 3-axis vertical-spindle machine profile does not have.
  cab1/hw.joint.bottom.side.right/op.6: Operation 'cab1/hw.joint.bottom.side.right/op.6' on face W2 of 'Bottom' is on an edge face; it needs a horizontal spindle or an angle aggregate, which a 3-axis vertical-spindle machine profile does not have.
CAM diagnostics:
  [warning] cam.profile.unverified (FurnitureOS demo profile — not a real machine): Machine profile 'FurnitureOS demo profile — not a real machine' (controller: 'fictional controller (demo profile — not a real machine)') is NOT verified: Demo profile — not a real machine. Not verified against any physical machine or controller.
fos cam: nothing to machine on face A of cab1/bottom; no program was written.
(exit code 1)
```

Beginning of `side-left-B.nc`:

```text
(==================================================================)
(GENERATED BY FURNITUREOS CAM - DO NOT RUN WITHOUT REVIEW)
(POSTPROCESSOR: Generic ISO G-code [RS-274 style], UNVERIFIED)
(THIS GENERIC DIALECT IS NOT VERIFIED AGAINST ANY CONTROLLER OR MACHINE.)
(IT MUST BE REVIEWED AND PROVEN BY A QUALIFIED OPERATOR ON THE ACTUAL)
(CONTROL BEFORE USE. FIRST RUN: DRY RUN ABOVE THE STOCK, HAND ON E-STOP.)
(MACHINE PROFILE: FurnitureOS demo profile — not a real machine / CONTROLLER LABEL: fictional controller [demo profile — not a real machine] / PROFILE NOT VERIFIED)
(PART: cab1/side.left - Left side - 720 x 560 x 18 MM)
(SETUP: FACE B UP - PANEL TURNED OVER ABOUT ITS LENGTH AXIS, FRONT EDGE L1 AT MAX Y)
(ORIGIN: WORK ZERO AS IN MACHINE PROFILE FRAME. STOCK MIN CORNER AT X20.000 Y20.000 Z0.000 = STOCK BOTTOM)
(STOCK: X20.000..740.000 Y20.000..580.000 Z0.000..18.000 LENGTH ALONG +X)
(SAFE Z 60.000 / CLEARANCE PLANE Z 23.000)
(TOOL T3: demo.drill.5 - drill D5 FLUTE 30 Demo drill 5 mm [fictional])
(SIMULATED: 480 CELLS REMOVED, EST. 15 S [ESTIMATE ONLY])
(==================================================================)
G21 G90 G17 G94
G0 Z60.000
T3 M6
S6000 M3
(OPERATION cab1/hw.hinges.door.left/op.2)
G0 X206.000 Y543.000 Z60.000
G0 X206.000 Y543.000 Z23.000
G1 X206.000 Y543.000 Z5.000 F1500.0
G0 X206.000 Y543.000 Z23.000
(OPERATION cab1/hw.hinges.door.left/op.3)
G0 X238.000 Y543.000 Z23.000
G1 X238.000 Y543.000 Z5.000 F1500.0
G0 X238.000 Y543.000 Z23.000
(OPERATION cab1/hw.pins.shelf.1/op.1)
G0 X418.500 Y510.000 Z23.000
…
```

### `fos export-obj`

Wavefront OBJ of the resolved model (every part a box at its finished size, project frame, mm, Z up) and `<stem>.mtl` beside it.

```text
$ fos export-obj kitchen.fosp kitchen.obj
Wrote 10 parts to kitchen.obj (+ MTL beside it)
(exit code 0)
```

## Tests

* `src/cli/tests/cli_tests.cpp` (`fos_cli_tests`): every command in-process on a temporary project — exit codes,
  messages, written files (CSV row text, SVG paper size, DXF, labels, OBJ object count, G-code header), refusals
  that leave the file unchanged, the simulator-error refusal of `fos cam` (3300 mm side outside the demo envelope),
  edge holes reported unsupported, determinism.
* `src/cli/tests/cli_binary_tests.cpp`: runs the real `fos` executable as a process (exit codes 0/1/2, output).
* `tests/` (`fos_scenario_tests`): cross-module end-to-end scenarios A–E with hand-computed expectations; built by
  the root CMakeLists for a full configure and by this directory's CMakeLists when `FOS_MODULES` lists `cli`.

## Limitations

* DXF export writes the first view only (front view / face A) at 1:1; the full sheet is available as SVG.
* `fos cam` uses fictional demo data only; there is no option to load a real machine profile or tool library, and
  no output of this tool has been run on, or proven against, any machine.
* `fos set` edits parameters only; structure edits (sections, fronts, materials) are not exposed on the command
  line yet.
* No PDF output (the Qt `printing` module provides it in GUI builds).
