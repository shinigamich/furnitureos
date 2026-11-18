<!-- SPDX-License-Identifier: AGPL-3.0-only -->
# `project` — the project document and the native `.fosp` format

Module `fos::project` (depends on every engineering module; third-party: nlohmann/json via `formats`).
This file is the **public documentation of the `.fosp` payload** and of the command / undo model.

| Header | Contents |
|---|---|
| `project.hpp` | `Project`, `Metadata`, `Item` (cabinet or `PanelItem`), `CuttingSettings`, `Remnant`, `ProjectSettings`, `newProject`, generic default libraries, drawing ids |
| `resolve.hpp` | `resolve(project) -> Resolved` (domain::Model + per-item diagnostics), `validateStructure`, `generateItem` |
| `codec.hpp` | JSON codecs of every project type (`projectToJson` / `projectFromJson`, …) |
| `file.hpp` | `.fosp` spec + migrations, `serializeProject`, `saveProject`, `loadProject`, `parseProject`, autosave, recovery |
| `commands.hpp` | `Command`, `CommandLabel`, `Affected`, factory functions in `fos::project::commands` |
| `document.hpp` | `Document`: current project, undo/redo, dirty flag, change notification |
| `outputs.hpp` | `productionReports`, `cuttingPlans`, `assemblyDrawing`, `partDrawing` (thin glue) |

## Data flow

`Project` is the only authoritative, saved data. `resolve()` is pure and deterministic: every item is generated
by the furniture module (`furniture::generate`, `furniture::generatePanel`) against the project libraries; the
resulting `domain::Model` holds copies of the libraries and one assembly per generated item (id = item id,
placement = item placement, project order). Reports, cutting plans and drawings are always produced from a fresh
`Resolved`; nothing derived is stored. `resolve()` fails only for structural problems (empty/duplicate item or
library ids); an item the generator refuses is reported as `project.item.generation_failed` (Error) and left out
of the model, and two generated parts with the same id give `project.part.duplicate_id`.

New projects (`newProject`) get the generic built-in hardware catalogue (`hardware::builtinCatalog()`) and these
**generic placeholder** materials / edge bands (no manufacturer, cost 0 = unknown):

| Id | Item |
|---|---|
| `mat.generic.pb18` | Particleboard 18 mm, 2800 × 2070, no grain |
| `mat.generic.mdf18` | MDF 18 mm, 2800 × 2070 |
| `mat.generic.hdf3` | HDF 3 mm, 2800 × 2070 |
| `eb.generic.abs2` | ABS 2 × 22 mm |
| `eb.generic.abs04` | ABS 0.4 × 22 mm |

## 1. File envelope

A `.fosp` file is a `formats` container (src/formats/README.md §1: envelope, SHA-256 checksum of the canonical
payload, load pipeline, atomic save, backups, autosave):

| Envelope key | Value |
|---|---|
| `format` | `"furnitureos-project"` |
| `formatVersion` | `1` |
| `app` | `{"name": "FurnitureOS", "version": "<build version>"}` |
| `payload` | the object described below |

**Versioning / migration policy.** `formatVersion` is increased only when a version-N reader would misinterpret
or reject the payload; additive optional keys do not bump it. Each bump adds exactly one step N → N+1 in
`registerProjectMigrations()` (src/file.cpp); steps run sequentially on load, must keep unknown keys, and the
file on disk changes only when the user saves. Files newer than supported are refused (`UnsupportedVersion`)
and never partially loaded. No downgrade. Version 1 has no migrations.

**Load** = read → container checks (kind, version, checksum, migrations) → strict payload decode → `resolve()`.
Generator warnings or an item that cannot be generated are returned as diagnostics, never as a refusal.
`LoadOptions::allowChecksumMismatch` opens a damaged-checksum file with a warning (explicit recovery opt-in).

**Save** = encode → decode again (a project that would not load back, e.g. a non-rigid placement, is refused and
nothing is written) → `formats::saveDocument` (temp file, fsync, verify, rotate `settings.backupCount` backups,
atomic rename). Saving an unchanged loaded project reproduces the file byte for byte (same `savedAt`).

## 2. Payload schema

Conventions: lengths in mm; numbers are JSON numbers written as round-trip doubles; ids are strings. Error
messages start with a JSON-pointer path, e.g.
`/payload/items/0/interior/children/1/shelfCount: expected an integer >= 0, got -1`.

**Unknown keys.** Objects marked *(extensible)* keep unknown keys (in `extra`, in file order) and write them back
after the known keys. Library entries (Material, EdgeBand, HardwareDefinition) follow the formats convention
(`custom["json:<key>"]`). **Every other object is closed**: an unknown key is an error naming the key and the
allowed keys — never silently dropped.

### 2.1 Root *(extensible)*

| Key | Type | Req. | Meaning |
|---|---|---|---|
| `project` | Metadata | R | |
| `libraries` | Libraries | | default: empty |
| `items` | array of Item | | project order; ids unique |
| `drawingEdits` | object: drawing id → DrawingEdits | | |
| `cutting` | ProjectCutting | | |
| `settings` | Settings | | |
| `custom` | object | | user data, preserved verbatim |

### 2.2 Metadata (`project`) *(extensible)*

`id` (R, non-empty string; UUID v4), `name` (R), `description`, `author`, `currency` (default `"EUR"`),
`units` (R, must be `"mm"`), `createdAt`, `modifiedAt` (ISO-8601 UTC strings supplied by the application; may be
empty).

### 2.3 Libraries — closed

`materials`, `edgeBands`, `hardware`: arrays of formats Material / EdgeBand / HardwareDefinition (formats README
§3.4–3.6); ids unique within each list.

### 2.4 Item *(extensible)*

`type` (R): `"cabinet"` or `"panel"`.

**Cabinet** (`furniture::CabinetDefinition`):

| Key | Type | Req. | Meaning |
|---|---|---|---|
| `id` | string | R | assembly id; part ids are `<id>/<slot>` |
| `name` | string | R | |
| `placement` | Placement (formats §3.3) | | assembly frame → project frame; identity |
| `construction` | Construction | | |
| `materials` | MaterialAssignment | | |
| `hardware` | HardwareSelection | | |
| `parameters` | array of Parameter | R | the parameter graph (`ParameterGraph::definitions()`, sorted by name) |
| `constraints` | array of Constraint | | sorted by name (includes the `std.*` constraints) |
| `pinnedParameters` | array of string | | `CabinetDefinition::pinnedParameters` (sorted) |
| `interior` | Section | R | root of the section tree |

**Panel** (`PanelItem` = `furniture::PanelDefinition` + placement): `id` (R, assembly id), `name` (R),
`placement`, `partId` (R, non-empty; new panels use `<id>/panel`), `role` (PartRole token, formats §3.1, default
`generic`), `orientation` (`Horizontal` | `VerticalSide` | `VerticalFront`), `material` (R, id), `edges` (Edges),
`parameters` (R), `constraints`, `contour` (Contour, optional; absent = rectangle).

Enumerations of the furniture module are written with their **stable `toString()` texts** (case-sensitive):

| Key | Tokens |
|---|---|
| `topJoint`, `bottomJoint` | `BetweenSides`, `OverSides` |
| `top` | `Panel`, `Rails` |
| `back` | `None`, `Overlay`, `Grooved`, `Inset` |
| `plinth` | `None`, `SidesToFloor`, `Feet` |
| `topFastener`, `bottomFastener`, `dividerFastener` | `None`, `Confirmat`, `Dowel` |
| `split` | `None`, `Vertical`, `Horizontal` |
| size `kind` | `Fixed`, `Weight`, `Expression` |
| front `kind` | `Door`, `DoorPair`, `DrawerStack` |
| `overlay` | `Full`, `Half`, `Inset` |
| `hingeSide` | `Left`, `Right` |
| `handle` | `None`, `Center`, `Top`, `Bottom` |

**Construction** — closed: the keys above plus `shelfSupports` (bool). Defaults = `furniture::ConstructionOptions{}`.

**MaterialAssignment** — closed: `carcass`, `back`, `front`, `visibleEdge` (strings, `""` = none / same as
carcass / raw, see furniture `definition.hpp`), `hiddenEdge`, `frontEdge` (string or `null` = unset),
`overrides` (object slot id → `{ "material": id|null, "edges": { "l1"|"l2"|"w1"|"w2": id|null } }`; a present
edge key with `null` means "raw edge", an absent key means "no override"). `drawer`, `drawerBottom`
(drawer-box materials, strings; written only when assigned — absent = the defaults of furniture `definition.hpp` —
so older files are unchanged byte for byte). An override may also carry
`"contour"`: Contour (written only when set, so files without contours are unchanged byte for byte).

**Contour** (`furniture::ContourSpec`, furniture README "Contoured parts") — closed; every key is written
(lossless, also the fields the kind does not use): `kind` (R: `Rectangle` | `RoundedCorners` | `ArcEdge` |
`Custom`), `radii` (array of exactly 4 expression strings, corners (0,0), (L,0), (L,W), (0,W); `""` = 0),
`arcEdge` (`l1` | `l2` | `w1` | `w2`), `arcRise` (expression), `arcDirection` (`Convex` | `Concave`),
`vertices` (array of `{ "x": expr (R), "y": expr (R), "bulge": number, "bandFrom": "l1"…|null }`, ≤ 500),
`cutouts` (array of `{ "kind": "Rectangle"|"Circle" (R), "x", "y", "length", "width", "radius" }` expression
strings, ≤ 50). Decoding runs `furniture::checkContourSpec` (expressions must parse; Custom ≥ 3 vertices,
|bulge| ≤ 1); values are checked when the item is generated.

**HardwareSelection** — closed: `hinge`, `handle`, `shelfPin`, `confirmat`, `dowel`, `foot` (catalogue ids;
defaults = the generic built-in ids).

**Parameter** — closed: `name` (R), `kind` (`Number` `Length` `Count` `Angle` `Ratio` `Boolean`, default
`Number`), **exactly one of** `expression` (non-empty text, parametric grammar) or `value` (number), `description`,
`lowerBound`, `upperBound` (number or null/absent), `integer` (bool). The graph is rebuilt with
`ParameterGraph::load`; reference errors, cycles, bounds and Error-constraints are reported at `…/parameters`.

**Constraint** — closed: `name` (R), `expression` (R), `message`, `severity` (`error` | `warning`).

**Section** — closed: `size` (`{kind, value, expression}`; Fixed/Weight need `value > 0`, Expression a
non-empty `expression`), `split`, `shelfCount` (integer 0 … 100), `front` (Front, absent/null = none),
`children` (array of Section, at most 50). Front — closed: `kind` (R), `overlay`, `hingeSide`, `hinges` (bool),
`handle`, `drawerHeights` (array of size objects, bottom to top, at most 30), `drawerBox` (closed object
`{ "slide": hardware id (R, non-empty), "bottom": "Grooved" | "NailedUnder" }`; written only when set, absent =
fronts only, so files without drawer boxes are unchanged byte for byte). Section-tree rules (child counts,
sizes) are checked by the generator and reported as item diagnostics, except the hard limits of §2.8, which reject
the file.

**Edges** — closed: `l1`, `l2`, `w1`, `w2` → edge band id or `null` (raw).

### 2.5 DrawingEdits — closed

Keyed by drawing id: `<item id>/drawing.assembly` (assembly drawing) or `<part id>/drawing.part`.

| Key | Content |
|---|---|
| `elements` | object element id → `{offset: number|null, textShift: [u, v], hidden: bool, textOverride: string|null, measuredAtEdit: number|null}` |
| `texts` | `[{id, viewId, position: [u, v], text, height (> 0), rotationDeg}]` |
| `leaders` | `[{id, viewId, target: [u, v], textOffset: [u, v], text, height (> 0)}]` |
| `dimensions` | `[{id, viewId, orientation: "horizontal"|"vertical", p1, p2, offset, textOverride: string|null, anchor1, anchor2, measuredAtEdit: number|null}]`; anchor = `null` or `{partId, corner: integer|null, localPoint: [x, y, z]}` |
| `detailMarkers` | `[{id, viewId, center: [u, v], radius (> 0), label}]` |

Semantics (orphans, stale overrides, anchors) are those of `drawings/edits.hpp`; the project stores edits even
when they no longer match anything (the drawing reports them as orphans).

### 2.6 Cutting (`cutting`) — closed

`defaults`: CuttingSettings; `materials`: object material id → CuttingSettings (overrides the defaults for that
material). CuttingSettings — closed: `kerf` (≥ 0, default 4), `trim` (`{left, right, top, bottom}` ≥ 0, all
required; default 10 each), `minRemnantLength` (300), `minRemnantWidth` (100), `maxStages` (0 = unlimited or ≥ 2),
`firstCut` (`free` | `lengthwise` | `crosswise`), `objective` (`minimize_sheets` | `minimize_waste` |
`prefer_remnants` | `minimize_cuts`), `remnants` (`[{id (unique), label, length > 0, width > 0, quantity ≥ 1}]`;
length runs along the sheet length / grain).
`nesting` (optional; absent = defaults and **not written**, so older files stay byte for byte the same; when
present every key is written, also default values — lossless): NestingSettings of the shaped (routed) parts of
all materials — closed: `toolDiameter` (0 < d ≤ 100, default 8), `clearance` (0 … 100, 0), `chordTolerance`
(0.01 … 10, 0.1), `margins` (`{left, right, bottom, top}`, each 0 … 1000, all required; default 10 each),
`minRemnantLength` (0 … 100 000, 300), `iterations` (integer 0 … 1000, 20; bounds the work a file can request),
`seed` (integer 0 … 4294967295, 12345). Values are checked on load and by `setNestingSettings` with
`checkNestingSettings` (message names the field and the value).

### 2.7 Settings *(extensible)*

`backupCount` (integer 0 … 100, default 3: backups kept on save), `edgeBanding` (closed:
`{overhangPerEnd ≥ 0 (25), wastePercent ≥ 0 (0)}` for the edge banding report),
`cutListGroupAcrossAssemblies` (bool, default true).

### 2.8 Hard limits

A hostile or corrupted file must not hang loading, generation or saving. Files beyond these limits are **rejected
on load** with a ParseError naming the JSON path and the numbers (e.g. `/payload/items/0/interior/shelfCount:
expected an integer from 0 to 100, got 2000`); the same limits refuse Document commands (`validateStructure`) and
furniture edits (`furniture::generate`). Counts of arrays are checked before their entries are decoded.

| Limit | Value | Where defined | Checked |
|---|---|---|---|
| file size | 256 MiB (`formats::kDefaultMaxFileBytes`, `LoadOptions::maxFileBytes`) | formats | before reading, regular files only |
| items per project | 500 | `project::limits::kMaxItems` | array length on load, `validateStructure` |
| estimated parts per project | 20 000 (sum of `furniture::estimatePartCount`, panels 1) | `project::limits::kMaxEstimatedParts` | after decoding, `validateStructure` |
| `settings.backupCount` | 0 … 100 | `project::limits::kMaxBackupCount` = `formats::kMaxBackupCount` | on load, `setSettings`, `saveDocument` |
| shelves per section | 100 | `furniture::limits::kMaxShelvesPerSection` | on load (`shelfCount`), generator |
| adjustable shelves per cabinet | 200 | `furniture::limits::kMaxShelvesPerCabinet` | after decoding the cabinet, generator |
| children per split section | 50 | `furniture::limits::kMaxSectionChildren` | array length on load, generator |
| sections per cabinet (root included) | 250 | `furniture::limits::kMaxSections` | after decoding, generator |
| section nesting depth (root = 0) | 16 | `furniture::limits::kMaxSectionDepth` | after decoding, generator |
| drawer fronts per stack | 30 | `furniture::limits::kMaxDrawerHeights` | array length on load, generator |
| parts per cabinet | 2 000 (estimate: 8 + dividers + shelves + fronts; and the generated count) | `furniture::limits::kMaxPartsPerAssembly` | after decoding, generator |
| standard (length) parameters, Fixed sizes | finite, \|value\| ≤ 100 000 mm | `furniture::limits::kMaxDimension` | after decoding (constant and evaluable values), generator |
| Weight sizes | ≤ 1 000 000 | `furniture::limits::kMaxWeight` | after decoding, generator |
| generated part length / width / thickness | finite, > 0, ≤ 100 000 mm | generator (`furniture.geometry.invalid_size`) | item diagnostic `project.item.generation_failed` |
| panel item parameters | finite, \|value\| ≤ 100 000 mm | `furniture::generatePanel` | item diagnostic |
| contour vertices / cut-outs | 500 / 50 per contour; ~10 000 segments per cabinet | `furniture::limits::kMaxContour*` | array length on load, after decoding, generator |

The JSON nesting of the whole file is limited to 128 levels by the container (formats §1.2).

## 3. Commands and undo

* A `Command` = `CommandLabel` (English key with `{0}`, `{1}` … placeholders + arguments, so the app can translate
  the key with `tr()`), an `apply(Project&) -> Result<Affected>` function, and an optional `mergeKey`.
  Factories in `fos::project::commands` cover parameters, all furniture structure edits (via
  `furniture/edit.hpp`, which is atomic), panels, item add / remove / duplicate / move / rename, library
  upsert / remove (removal is refused while an item references the entry), drawing edits, cutting settings,
  metadata, settings and custom data. Labels name items by id (the UI may map ids to names).
  Contours: `setPartContour(item, slot, spec|nullopt)` (cabinets, via `furniture::setPartContour`) and
  `setPanelContour(item, spec|nullopt)` (panels; the panel is generated first, an invalid contour fails the
  command and changes nothing).
  Nesting: `setNestingSettings(settings|nullopt)` (nullopt removes the key = defaults).
* `Document::execute` runs the command on a **copy** of the project, checks the structure, and re-generates every
  affected item that generated before; any error leaves project, history and dirty flag untouched. A command that
  changes nothing records no step.
* **Undo by whole-project snapshot**: each record keeps the project before and after (cheap: parameter graphs are
  copy-on-write). Undo/redo assign the snapshot — no inverse operations exist that could be wrong. History is
  bounded (`setUndoLimit`, default 200).
* **Merging** is opt-in per call (`Merge::WithPrevious`): consecutive commands with the same non-empty `mergeKey`
  (`parameter:<item>:<name>`, `placement:<item>`, `element:<drawing>:<element>`, `section-size:<item>:<path>`)
  collapse into one step, never across the save point.
* **Dirty flag**: clean exactly when the current state is the saved one (construction, `save`, `markSaved`).
  Undo back to the save point is clean; once the save point is discarded (new command after undo, history
  limit) the document stays dirty until saved. `autosave` does not change the flag.
* **Change notification**: observers receive `ChangeNotice{kind (Execute/Undo/Redo), label, affected}` where
  `Affected` lists item ids (library edits list every item), changed drawing ids and `libraries` / `settings`
  flags. Failed commands notify nothing.

## 4. Outputs (thin glue)

* `productionReports(resolved, productionOptions(project))`: part list, cut list, material, edge banding, hardware,
  machining and summary reports from `production`.
* `cuttingPlans(resolved, project.cutting)`: `cutting::jobsFromModel` with the default settings, per-material
  settings applied to each job (job id = material id), remnants appended as limited stock
  `<material>/remnant/<id>` (isRemnant, priority 1, cost 0), then `cutting::optimize` (a heuristic) and the
  independent `cutting::validateLayout`. Diagnostics: adapter findings, `project.cutting.optimize_failed`,
  `project.cutting.layout_invalid`, `project.cutting.unplaced`.
* Shaped parts (ADR-0008, `!domain::isRectangularOutline`): the guillotine side uses `cutting::cuttingFromModel`
  (shaped parts are left out and reported as Info `cutting.adapter.routed`, listed in `CuttingPlans::routedParts`);
  `nesting::nestingJobsFromModel` with `nestingModelSettings(project.cutting.nestingSettings())` builds one
  nesting job per material, then `nesting::nest` (a **heuristic**; `iterations`, `seed` from the settings) and the
  independent `nesting::validateNest`. Result: `CuttingPlans::nestingPlans` (`NestingPlan{job, result,
  validation}`), `materials` (model material order, `MaterialPlans{materialId, guillotine?, nesting?}` — a
  material can have both), `guillotineFor(id)` / `nestingFor(id)`. Diagnostics: `nesting.adapter.*`,
  `project.nesting.failed` (Error), `project.nesting.layout_invalid` (Error, one per validateNest error),
  `project.nesting.unplaced` (Warning). `plans` is unchanged for rectangular projects.
* `assemblyDrawing(project, resolved, itemId, options)` / `partDrawing(...)`: `drawings::generate*Drawing` with the
  stored edits of the drawing id (model anchors are resolved by the drawings module).

## 5. Limitations

* The per-project parts limit is checked on the estimate (`estimatePartCount`), an upper bound; custom parameter
  values are not limited (sizes derived from them are checked on the generated parts).
* Rejecting a file at the 500-item limit still parses the whole (≤ 256 MiB) JSON text first.

* Undo snapshots cost memory proportional to the project size per step (bounded by the undo limit).
* Labels carry item ids, not display names.
* `recoverProject` returns the newest autosave/backup that passes the container checks; if its payload then fails
  to decode the error is returned (older candidates are not tried).
* No `.fosp` migrations exist yet (version 1); the migration path is exercised only by the formats module tests.
* The checksum is an integrity check, not a signature (formats README §1.1).

## 6. Tests

`fos_project_tests`: default libraries; base-cabinet part and cut sizes (hand-computed); panel items and
placements; generation failure as diagnostic; structural errors; determinism; Scenario B (width 800 → 900 by
command: bottom/rails/plinth/shelf/back +100, cut list rows, validated cutting plan, drawing overall width 900;
undo restores the identical resolved model; redo); stored drawing edits applied; per-material cutting settings and
remnants; save → load → save byte-identical with every schema feature (incl. anchored user dimensions and
`measuredAtEdit`); unknown-key preservation; closed-object rejection with paths; strict validation messages;
newer version / other kind refused; truncation at many offsets; checksum mismatch and recovery opt-in; unsavable
project not written; autosave + recovery; failed commands leave the document unchanged; dirty flag; merging;
undo limit. `nesting_tests.cpp`: nesting settings absent → not written, explicit defaults and non-default values
(seed > INT_MAX) round-trip byte for byte, range errors on load and in the command, cutting plans with a shaped shelf
(nested, part spacing 6 + 0 + 2·0.1 = 6.2 with a 6 mm tool, both validators clean). Scenario F (tests/) covers
the whole chain. `limits_tests.cpp`: the reviewer's hostile files (shelfCount 2000 / 10000, 51 children, 3 × 100 shelves,
31 drawers, height 1e308, backupCount 1e6, 501 items) are rejected on load in well under a second with the exact
messages; commands refuse the same limits; backup rotation through `saveProject`.
