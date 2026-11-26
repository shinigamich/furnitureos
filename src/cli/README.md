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
| 1 | the command ran and found errors: validation errors, a refused parameter, a refused CAM program, a drawing that does not fit at the requested scale, a cutting layout with validator errors, an output command run on a project with model errors (incomplete output) |
| 2 | usage or I/O error: unknown command/option, missing argument, unknown item/part id, unreadable/corrupt project file, unwritable output |

**Incomplete output is never silent.** Every output command (`report`, `cut`, `drawing`, `labels`, `cam`,
`export-obj`) checks the resolve diagnostics of the project: when they contain errors (e.g. an item the
generator refuses, so it has no parts) it still writes the output of the good items, but prints
`fos <command>: INCOMPLETE OUTPUT: … Item(s) that could not be generated and are MISSING from this output: <ids>.`
followed by every error diagnostic to stderr (`fos cut` also puts it into `cutting-summary.txt`) and exits 1.

Every command prints its usage with `--help`; `fos --help` lists the commands. There is no separate `fos nest`:
`fos cut` nests the shaped parts.

## Commands

All output below is real output of this build, captured on the reference project
(`fos new … --template base --width 800 --height 720`, then widened to 900 by `fos set`). Very long lines are
shown in full; `…` marks lines removed from long listings.

### `fos new`

Creates a `.fosp` project with the generic placeholder libraries (no manufacturer, cost 0 = unknown). `--template base|wall|tall|drawers|drawers-boxed|wardrobe|shelving|corner-blind|corner-l|oven-tall|dishwasher` adds one cabinet (`drawers` = fronts only; `drawers-boxed` = the same unit with a drawer box on generic ball-bearing slides behind every front; `corner-blind` = blind corner base cabinet 1000 wide with a 500 mm blind panel on the left; `corner-l` = L-shaped corner base cabinet, legs 900 x 900, `--width`/`--depth` set leg A; `oven-tall` = tall housing with two drawers, an open oven niche (595 mm, generic) and a door above; `dishwasher` = 600 mm appliance gap with an end panel on the right and a decor front fixed to the appliance door — appliance sizes are generic, check the appliance's installation sheet) (item id `cab1`, or `--item-id`); `--width/--height/--depth` are applied through the same undoable document commands the desktop application uses. `--id`/`--date` make the file reproducible (default: random UUID v4, current UTC time). An existing file is never overwritten without `--force`.

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

`--type parts|cutlist|boughtin|materials|edgebanding|hardware|machining|summary` prints a text table followed by the report's diagnostics; `--csv out.csv [--separator ';']` writes RFC 4180 CSV instead; `--html out.html <item-id>` writes the printable production sheet of one item. Cut sizes are saw sizes (finished size minus edge banding, ADR-0004). Sheet counts in the materials report are *area lower bounds*. Bought-in items (glass panes of glass-panel fronts, ordered cut to size) are left out of the cut list, materials, edge banding and machining reports and listed by `--type boughtin` (grouped by size and material); `fos quote` prices them by area (`Bought-in` lines).

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

**Shaped parts** (ADR-0008: arcs, non-rectangular corners or cut-outs) are not sawn. `project::cuttingPlans` leaves
them out of the guillotine jobs (Info `cutting.adapter.routed`) and nests them true-shape per material with the
project nesting settings (`.fosp` `cutting.nesting`, project README §2.6; defaults tool 8 mm, clearance 0, chord
tolerance 0.1, margins 10, 20 iterations, seed 12345). The nesting is also a **heuristic**; every layout is checked by
`nesting::validateNest`; nest sheets are written as `<dir>/<material>-nest-<n>.svg`. A material can have both kinds.
Exit 1 also for nesting failures or validateNest errors. Output for a project with a shaped shelf (R 30 front
corners) and a 1200 × 600 worktop with an R 20 sink cut-out and a Ø 35 tap hole (contours set with the project
commands; the CLI has no contour editing):

```text
$ fos cut kitchen.fosp --out-dir cutting
Cutting plans of 'Kitchen'
Layouts are produced by a HEURISTIC optimiser; utilisation is that of the layout found, which is in general not optimal.
Shaped parts are nested by a true-shape HEURISTIC for routing; those layouts are not optimal either.

Material mat.generic.pb18 "Particleboard 18 mm (generic)" (18 mm)
  kerf 4 mm, trim left/right/bottom/top 10/10/10/10 mm
  pieces: 8 required, 8 placed, 0 unplaced
  sheet 1: mat.generic.pb18/sheet 2800 x 2070 mm, 8 pieces, utilisation 33.42 % of the sheet (33.99 % of the usable area), 16 cuts -> mat.generic.pb18-sheet-1.svg
  total: 1 sheet(s), placed area 1.937 m2 of 5.796 m2 sheet area = utilisation 33.42 % (heuristic result)
  sheet count lower bound (area): 1 - sheet count proven minimal
  strategy: heuristic: sort=longer-side, split=shorter-leftover-axis, fit=best-area-fit, orientation=best-of-both, stock=largest-first
  validator (cutting::validateLayout): no findings
  SHAPED parts (routed, true-shape nesting HEURISTIC - not optimal): 2 part(s)
    tool 8 mm + clearance 0 mm, chord tolerance 0.1 mm -> part spacing 8.2 mm, edge spacing 8.1 mm; margins left/right/bottom/top 10/10/10/10 mm; 20 iterations, seed 12345
    parts: 2 required, 2 placed, 0 unplaced
    nest sheet 1: mat.generic.pb18/sheet, 2 part(s), used length 618.1 mm, utilisation 19.49 % of the sheet -> mat.generic.pb18-nest-1.svg
    total: 1 sheet(s), part area 1.1295 m2 (net of cut-outs 0.9289 m2) of 5.796 m2 = utilisation 19.49 % (heuristic result, 20 improvement iterations)
    validator (nesting::validateNest): no findings

Material mat.generic.hdf3 "HDF 3 mm (generic)" (3 mm)
  kerf 4 mm, trim left/right/bottom/top 10/10/10/10 mm
  pieces: 1 required, 1 placed, 0 unplaced
  sheet 1: mat.generic.hdf3/sheet 2800 x 2070 mm, 1 pieces, utilisation 8.46 % of the sheet (8.6 % of the usable area), 6 cuts -> mat.generic.hdf3-sheet-1.svg
  total: 1 sheet(s), placed area 0.4903 m2 of 5.796 m2 sheet area = utilisation 8.46 % (heuristic result)
  sheet count lower bound (area): 1 - sheet count proven minimal
  strategy: heuristic: sort=area, split=shorter-leftover-axis, fit=best-area-fit, orientation=best-of-both, stock=largest-first
  validator (cutting::validateLayout): no findings

Diagnostics: 2
  [info] cutting.adapter.routed (cab1/shelf.1): Part 'Shelf' (cab1/shelf.1) has a non-rectangular outline or cut-outs; it is not in any guillotine cutting job and must be nested and routed (see the nesting jobs).
  [info] cutting.adapter.routed (top/panel): Part 'Worktop' (top/panel) has a non-rectangular outline or cut-outs; it is not in any guillotine cutting job and must be nested and routed (see the nesting jobs).
Wrote 2 sheet SVG(s) and 1 nesting sheet SVG(s) and cutting-summary.txt to cutting
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

`--barcode code128` adds a Code 128 (set B, with checksum) barcode to each label in place of the
part-id text line; `--barcode-content part-id|project-part-id` chooses what it encodes (the part id,
default, or `<project>|<part id>`). Quiet zones of 10 modules are kept; when a label is too narrow for
modules of at least 0.25 mm the barcode is left out for that label and a warning
`production.label.barcode_too_narrow` is printed (exit code unchanged). The same options work for
`fos batch labels`.

### `fos quote`

```text
fos quote <file.fosp> [--format text|csv|json] [--sheets]
```

Customer quote (`production::quote`): materials, edge banding, hardware, labour (rates set in
`settings.pricing`), markup on the subtotal, tax on subtotal + markup. Prices come only from the
libraries (material `costPerM2` / `costPerSheet`, edge band `costPerMeter`, hardware `unitCost`);
the currency is `project.currency` (label only). Without `--sheets` materials are priced by cut
area + `wastePercent`; with `--sheets` the cutting plans are optimised as in `fos cut` (heuristic)
and materials are priced per purchased sheet (listed remnants not counted); the plans' saw cut
length (+ routing length of shaped parts) feeds `perCutMetre`, which is unpriced without a plan.
Lines without a price are listed as `UNPRICED` with a `production.quote.unpriced` warning and are
not in the total; the exit code is then 1. CSV/JSON go to stdout, diagnostics to stderr (JSON also
includes them); JSON money totals are 2-decimal strings.

### `fos packing`

```text
fos packing <file.fosp> [--max-kg 25] [--max-length mm] [--max-width mm] [--max-height mm]
            [--padding 10] [--format text|csv|json] [--svg dir]
```

Packing list for delivery / flat-pack (`production::packingList`): per item, the panels grouped
into packages (first-fit heuristic, largest panels at the bottom) within `--max-kg` and the optional
outer size limits (padding included; 0 = no limit), plus one hardware bag per item (fittings and
fasteners by definition and count). Package = largest panel + 2 × padding, height = stacked
thickness + 2 × padding. Weights come ONLY from the material `density` (kg/m³, optional library
field); without it panels and their packages are `weight unknown` (warning
`production.packing.density_unknown`) and are packed separately from weighed panels, limited only by
the size limits (use `--max-height` to split them). Weights exclude packaging and hardware. CSV/JSON
go to stdout (CSV diagnostics to stderr; JSON includes them, unknown weights are `null`). `--svg`
writes package labels (one per package and per hardware bag, 70 × 37 mm part-label geometry) as
`DIR/package-labels-<n>.svg`. Invalid limits are usage errors (exit 2).

### `fos edgeband-jobs`

`fos edgeband-jobs <file.fosp> [--format text|csv|json] [--svg DIR] [--assembly ID]` prints the
edge-banding job list for the banding machine operator (`production::edgeBandingJobs`): grouped by
band, per part the cut size and per banded edge the cut edge length, edge length, strip length incl.
the project's overhang per end and a suggested step (long edges first, then short: common practice,
not a machine requirement); totals per band; shaped parts listed separately for contour banding.
CSV/JSON go to stdout with diagnostics on stderr. `--svg DIR` writes `edgeband-<band>[-<page>].svg`
(A4 per band). Not to be confused with `fos edgeband-rolls` (roll planning).

### `fos assembly`

```text
fos assembly <file.fosp> [--svg dir]
```

Assembly sheet per item (`production::assemblySheets`): numbered steps with parts and hardware
counts. The order is GENERIC, from part roles and hardware kinds only (carcass → back →
shelves/dividers → drawer boxes → fronts → handles → other), and every sheet says so; it is not a
verified instruction. `--svg` writes `DIR/assembly-<item id>.svg` (A4 width, text only).

### `fos batch`

Production batches: several projects (orders) produced together (`.fosbatch`, schema in
src/project/README.md "Production batches").

```text
fos batch new <file.fosbatch> --name N [--date D] [--notes T]      # refuses to overwrite
fos batch add <file.fosbatch> <project.fosp> [--qty K] [--key KEY] [--note T]
fos batch report <file.fosbatch> --type cutlist|summary|hardware [--csv]
fos batch cut <file.fosbatch> [--out-dir DIR]
fos batch labels <file.fosbatch> --svg-dir DIR [--barcode none|code128] [--barcode-content part-id|project-part-id]
fos batch quote <file.fosbatch> [--format text|csv|json] [--sheets]
```

`quote` prices the merged batch (all copies) like `fos quote`, with the pricing settings and
currency of the first included project (a project with different ones: warning
`cli.quote.pricing_mismatch`) and the batch cutting settings for `--sheets`.

`add` stores the project path relative to the batch file and checks that the project loads;
adding an already listed project adds K to its quantity. Part ids in every output are
`<project key>/<part id>` (`<key>#<n>/<part id>` for the copies of a quantity > 1 project).
`cut` writes the same SVGs and `cutting-summary.txt` as `fos cut` (shared code), with the batch
cutting settings. `labels` prints batch, project and order copy on each label. A missing or
broken project, a library conflict (same id, different data) or a refused material substitution
is printed (`INCOMPLETE BATCH`, project marked `[LEFT OUT]`), the other projects are still
produced and the exit code is 1. With `--csv` the batch header and diagnostics go to stderr.

### `fos cam`

UNVERIFIED 3-axis program for the operations on face A or B of one part, using the FICTIONAL **"FurnitureOS demo profile — not a real machine"** and a fictional tool library (`fos/cli/demo_cam.hpp`: drills 3 4 5 7 8 10 12 15 20 35 mm, end mills 4 6 8 mm; stock min corner at machine (20, 20, 0)). The program is simulated independently and the generic ISO postprocessor refuses output when the simulation reports any error (exit 1). Operations on the other face are listed as needing the other setup, edge operations and pockets as UNSUPPORTED — never silently dropped. `verifiedForMachine` is always false. `--tools FILE.fostools` uses a tool table file instead of the demo tool library (see `fos tools`; output then shows `Tools: FILE (user tool table, ...)`).

```text
$ fos cam kitchen.fosp cab1/side.left --face B --out side-left-B.nc
Part cab1/side.left "Left side" 720 x 560 x 18 mm, 15 operations, face B up
Machine: FurnitureOS demo profile — not a real machine (fictional, verified: no)
Tools: fictional demo tool library
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
Tools: fictional demo tool library
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

**Shaped parts:** `fos cam` routes the raw outline (and every cut-out, first) with the cam module's contour routing
(radius compensation in the program, exact G2/G3 arcs, climb), passing the resolved model to `CamOptions::model` and
`SimulationOptions::model` (edge bands of the raw outline). The clamped blank is the part's bounding box grown by
`--blank-allowance` per side (default **10 mm** for shaped parts, 0 … 1000). A shaped part without other operations on
that face still gets a program (the contour). `--blank-allowance > 0` on a rectangular part is refused (exit 2) — such
parts are sawn to size. Any simulation error refuses the program (exit 1, nothing written).

```text
$ fos cam kitchen.fosp cab1/shelf.1 --face A --out shelf-A.nc
Part cab1/shelf.1 "Shelf" 762 x 540 x 18 mm, 0 operations, face A up
Machine: FurnitureOS demo profile — not a real machine (fictional, verified: no)
Tools: fictional demo tool library
Shaped part: outline routed; blank allowance 10 mm -> blank 782 x 560 x 18 mm
Machining validation: no findings
Machinable in this setup: 0
Needs the other face up: 0
UNSUPPORTED (not in this program): 0
CAM diagnostics:
  [warning] cam.profile.unverified (FurnitureOS demo profile — not a real machine): Machine profile 'FurnitureOS demo profile — not a real machine' (controller: 'fictional controller (demo profile — not a real machine)') is NOT verified: Demo profile — not a real machine. Not verified against any physical machine or controller.
  [warning] cam.contour.rounded_corner (cab1/shelf.1): The outline ('contour.outline') of 'Shelf' has 2 sharp corner(s) the end mill cannot reach; they are left rounded with radius 4 mm (first at machine X 790, Y 60).
Contour routing: 1
  contour.outline (outline, tool outside)
Simulation: 0 errors, 0 warnings; 1 tool change(s), rapid 189.5 mm, cutting 13171.4 mm, estimated 275 s (estimate, no acceleration)
Wrote shelf-A.nc (Generic ISO G-code (RS-274 style), UNVERIFIED)
verifiedForMachine: false - UNVERIFIED output for a fictional demo machine; review before any use.
Review before use:
…
(exit code 0)
```

```text
$ fos cam kitchen.fosp cab1/side.left --face B --out x.nc --blank-allowance 10
fos cam: --blank-allowance applies to shaped (routed) parts only; 'cab1/side.left' is rectangular and is sawn to size, so its stock is the part itself.
Run 'fos cam --help' for usage.
(exit code 2)
```

Lines 16–32 of `shelf-A.nc` (lead-in arc, then the R 32 tool-centre arcs of the R 28 raw corners — R 30 finished minus
the 2 mm front band — with the 4 mm tool radius):

```text
(==================================================================)
G21 G90 G17 G94
G0 Z60.000
T13 M6
S18000 M3
(OPERATION contour.outline)
G0 X407.000 Y578.000 Z60.000
G0 X407.000 Y578.000 Z23.000
G1 X407.000 Y578.000 Z14.000 F1000.0
G3 X411.000 Y574.000 Z14.000 I4.000 J0.000 F3000.0
G1 X796.000 Y574.000 Z14.000 F3000.0
G1 X796.000 Y56.000 Z14.000 F3000.0
G1 X793.749 Y56.000 Z14.000 F3000.0
G2 X762.000 Y28.000 Z14.000 I-31.749 J4.000 F3000.0
G1 X60.000 Y28.000 Z14.000 F3000.0
G2 X28.251 Y56.000 Z14.000 I0.000 J32.000 F3000.0
G1 X26.000 Y56.000 Z14.000 F3000.0
…
```

### `fos export-obj`

Wavefront OBJ of the resolved model (every part a box at its finished size, project frame, mm, Z up) and `<stem>.mtl` beside it.

```text
$ fos export-obj kitchen.fosp kitchen.obj
Wrote 10 parts to kitchen.obj (+ MTL beside it)
(exit code 0)
```

### `fos export-dxf`

Part outlines of the resolved model as ASCII DXF R12 (mm, `$INSUNITS 4`) for CNC / nesting / cutting software
(format and layers: `src/formats/README.md` §5.4). Layers `OUTLINE` (raw outline before edge banding;
`--finished`: finished outline), `CUTOUTS`, `POCKETS`, `DRILL_<d>` (holes on face A + through holes, e.g.
`DRILL_5`, `DRILL_8_5`), `GROOVES`, `LABELS` (part id text); arcs are exact polyline bulges. Options:
`--one-file-per-part` (second argument is a directory, created; one `<part-id>.dxf` each, part frame unshifted),
`--part ID`, `--spacing MM` (gap between parts in a single file, default 50). Blind face-B and edge operations are
left out with a warning.

```text
$ fos export-dxf kitchen.fosp parts.dxf --part work-top/panel
Wrote 1 part(s) to parts.dxf
Outline: raw (before edge banding), DXF R12, mm.
(exit code 0)
```

Exit codes: 0 written; 1 a part cannot be exported or the model has resolve errors; 2 usage error (unknown
`--part`, bad option, missing output directory) or I/O error. The output re-imports with `fos import-dxf`.

### `fos import-dxf`

Adds a free horizontal panel whose outline and cut-outs come from an ASCII DXF file (rules: `src/formats/README.md`
§5.3; conversion shared with the app in `fos/project/dxf_panel.hpp`) and saves the project atomically. `--name` is required; `--material ID` and/or `--thickness T` choose the
material (thickness alone = first library material of that thickness, by id). Options: `--id`, `--layer`,
`--units mm|cm|m|in|ft`, `--tolerance MM` (chaining, default 0.01), `--skip-unsupported-cutouts`. The outline is
stored as a Custom `ContourSpec` with numeric vertices (editable afterwards); circle cut-outs become Circle
cut-outs, axis-parallel (rounded) rectangles Rectangle cut-outs. Any other cut-out shape refuses the import
(exit 1, nothing saved) unless `--skip-unsupported-cutouts` leaves it out with a warning.

```text
$ fos import-dxf kitchen.fosp worktop.dxf --name "Work top" --material mat.generic.pb18
Imported 'Work top' as item work-top (part work-top/panel), material mat.generic.pb18 (18 mm).
Outline: 1200 x 600 mm, 4 segments (0 arcs); cut-outs: 2 (1 circle, 1 rectangle).
DXF: 3 entities, unit mm, 3 closed outline(s).
Saved kitchen.fosp
(exit code 0)
```

Exit codes: 0 imported; 1 refused (no closed outline, unsupported cut-out, panel invalid) or saved with model
errors; 2 usage error, unreadable / malformed DXF (message names the DXF line), unreadable / unwritable project.

### `fos worktop`

```
fos worktop <file.fosp> --items ID[,ID...] [--overhang MM] [--left-overhang MM] [--right-overhang MM]
            [--back-gap MM] [--material ID] [--band ID | --no-band] [--band-ends] [--band-back]
            [--max-length MM] [--sink X:W:D[:R[:S]]] [--hob X:W:D[:R[:S]]] [--id ID] [--name N] [-o OUT.fosp]
```

Adds a worktop over a straight run of cabinets through `project::commands::addWorktop` (rules:
`src/project/README.md` §4c) and saves to `-o` / `--out` or in place. `--overhang` is the front overhang (default
20 mm). Cut-outs: centre X from the left face of the leftmost cabinet, size W × D, corner radius R, front setback
S (default centred). A run longer than the usable stock is split into pieces with BUTT JOINTS, printed with their
positions. Minimum distances are rules of thumb (50 mm edges, 100 mm free ends, 150 mm joints, 100 mm between
cut-outs).

```
$ fos worktop kitchen.fosp --items cab1,cab2,cab3 --overhang 25 --sink 700:600:450:10 --hob 1400:500:450 -o out.fosp
```

Exit codes: 0 added; 1 refused (rule violated, run not straight, unknown item, ...; nothing saved); 2 usage error,
unreadable / unwritable project.

### `fos plinth`

```
fos plinth <file.fosp> --items ID[,ID...] [--setback MM] [--height MM] [--returns left,right]
           [--material ID] [--max-length MM] [--id ID] [--name N] [-o OUT.fosp]
```

Adds a front plinth (toe-kick) under a straight run of cabinets on feet through `project::commands::addPlinth`
(rules: `src/project/README.md` §4d) and saves to `-o` / `--out` or in place. `--setback` (default 50 mm, a rule of
thumb) is measured behind the foremost carcass front; `--height` defaults to the cabinets' `plinth_height`.
Appliance gaps (dishwasher) interrupt the plinth at their end panels. Pieces longer than the usable stock are split
equally with BUTT JOINTS; `--returns` adds end returns.

```
$ fos plinth kitchen.fosp --items cab1,dw,cab3 --setback 60 --returns right -o out.fosp
```

Exit codes: 0 added; 1 refused (cabinet not on feet, run not straight, unknown item, ...; nothing saved); 2 usage
error, unreadable / unwritable project.

### `fos component`

Reusable components (`.foscomp`, schema: `src/project/README.md` "Component files").

```
fos component save <project.fosp> <item-id> <out.foscomp> --name N [--category C] [--tags a,b]
                   [--description D] [--author A] [--id ID] [--force]
fos component list <dir|file.foscomp>...
fos component insert <project.fosp> <file.foscomp> <component-name-or-id>
                   [--id ITEM-ID] [--at x,y,z] [--on-conflict keep|replace|rename]
```

* `save` embeds copies of the materials, edge bands and hardware the item references (self-contained); id
  default `comp.<slug of the name>`; an existing output is replaced only with `--force`.
* `list` searches directories recursively, prints components grouped by category (sorted by category, name),
  with kind, preview size (mm), tags and source file; unreadable files are reported on stderr and give exit 1.
* `insert` adds the component as a new item (default id: first free `comp1`, `comp2`, ...) at `--at` (mm, default
  0,0,0) through the undoable `project::insertComponent` command and saves atomically. Embedded entries that
  exist in the project with different data: `keep` (default) uses the project's entry, `replace` overwrites it,
  `rename` adds the component's entry as `<item-id>.libN`. Identical entries are never duplicated. Changed
  entries are listed.

Exit codes: 0 done; 1 `list` found unreadable files, or `insert` refused (item id in use, item does not
generate; nothing saved); 2 usage error, unreadable project or component file (message names the file and a JSON
pointer), unknown component name.

### `fos merge`

```
fos merge <target.fosp> <source.fosp> [--items a,b] [--policy keep|replace|rename] [--offset x,y,z] [-o out.fosp]
```

Imports furniture items (default: all) of the source project with the library entries they use
(`project::mergeProjects`) and saves atomically to `-o` (default: the target file). Clashing item ids are renamed
(`cab1` → `cab3` when `cab1`, `cab2` exist); `--offset` (mm) moves every imported item. Library entries with an
existing id but different content: `keep` (default) uses the target's entry, `replace` overwrites it, `rename`
adds the source entry as `merge.libN` and points the imported items at it. Prints the imported items (with new
ids) and every added / replaced / renamed / conflicting entry.

Exit codes: 0 saved; 1 refused (an item does not generate in the target, an id given twice; nothing saved); 2 usage error, unreadable
project, unknown item id or missing library entry (nothing saved).

### `fos diff`

```
fos diff <a.fosp> <b.fosp> [--model] [--format text|json] [--ignore-timestamps]
```

Prints what changed from A to B (`project::diffProjects`), grouped: metadata, library entries (added / removed /
changed field paths), items (added / removed / renamed; parameters old → new, materials, hardware, construction,
interior and fronts as field paths, placement moved / rotated), settings (cutting, pricing, room, other) and other
stored data. `--model` also generates both projects and compares parts by id (added / removed / finished size /
material / edge bands / grain / operation count) and hardware quantities. `--format json` for tools.

Exit codes: 0 identical; 1 different; 2 usage error, unreadable project (or, with `--model`, a project that cannot
be resolved).

### `fos tools`

CAM tool tables (`.fostools`, schema: `src/formats/README.md` section 4.1).

```
fos tools validate <file.fostools>        # every finding with its JSON path
fos tools list <file.fostools>            # T, id, type, diameter, flute, overall, depth/pass, rpm, feed, plunge, name
fos tools export-default -o <file.fostools> [--force]
```

`export-default` writes the built-in FICTIONAL demo tool library of `fos cam` (13 tools, not real tools) as a
starting point. `fos cam ... --tools FILE` replaces the demo library with the table (every tool then needs
rpm, feed and plunge rate); the machine profile stays the fictional demo profile. No table is checked against
real tools or any machine.

Exit codes: 0 ok (warnings allowed); 1 invalid tool table (structure or values; also for `fos cam --tools`);
2 usage error, unreadable file, existing output without `--force`.

## Tests

* `src/cli/tests/cli_tests.cpp` (`fos_cli_tests`): every command in-process on a temporary project — exit codes,
  messages, written files (CSV row text, SVG paper size, DXF, labels, OBJ object count, G-code header), refusals
  that leave the file unchanged, the simulator-error refusal of `fos cam` (3300 mm side outside the demo envelope),
  edge holes reported unsupported, determinism.
* `src/cli/tests/cli_shaped_tests.cpp`: a shelf with R 30 front corners — `fos cut` writes guillotine and nesting
  SVGs (8 sawn pieces, 1 nested part, part spacing 8.2 mm, validateNest clean); `fos cam` routes it (blank 782 × 560
  at the default allowance, 772 × 550 with `--blank-allowance 5`, G2/G3, simulation clean); the option is refused for
  a rectangular part and for negative values.
* `src/cli/tests/cli_component_tests.cpp`: `fos component save` / `list` / `insert` round trip (placement, refusals,
  malformed file reported), `fos new --template drawers-boxed`.
* `src/cli/tests/cli_worktop_tests.cpp`: `fos worktop` with sink + hob (one piece, two cut-outs, `-o` leaves the
  input untouched), a 4200 mm run split at 2100 mm, rule-of-thumb refusal, usage errors.
* `src/cli/tests/cli_plinth_tests.cpp`: `fos plinth` with two equal pieces and both returns, refusal of a
  sides-to-floor cabinet, usage errors.
* `src/cli/tests/cli_binary_tests.cpp`: runs the real `fos` executable as a process (exit codes 0/1/2, output).
* `tests/` (`fos_scenario_tests`): cross-module end-to-end scenarios A–F with hand-computed expectations; built by
  the root CMakeLists for a full configure and by this directory's CMakeLists when `FOS_MODULES` lists `cli`.

## Limitations

* DXF export writes the first view only (front view / face A) at 1:1; the full sheet is available as SVG.
* `fos cam` uses fictional demo data only; there is no option to load a real machine profile or tool library, and
  no output of this tool has been run on, or proven against, any machine.
* `fos set` edits parameters only; structure edits (sections, fronts, materials) are not exposed on the command
  line yet.
* No PDF output (the Qt `printing` module provides it in GUI builds).
