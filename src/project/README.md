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
§3.4–3.6); ids unique within each list. A material may additionally carry `costPerM2` (number ≥ 0,
price per m² in the project currency, `domain::Material::costPerSquareMeter`); it is handled by the
project codec, written only when > 0, so files without it are unchanged. (The standalone formats
material codec does not know the key; it keeps it as a preserved unknown key.)

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
`cutListGroupAcrossAssemblies` (bool, default true), optional `pricing` (closed; absent = not
configured and not written): `{wastePercent, markupPercent, taxPercent}` (each 0 … 1000, default 0)
and `labour` (closed; `perCutMetre`, `perEdgeMetre`, `perHole`, `perPart`, each ≥ 0 and written only
when set) — `production::PricingSettings` of customer quotes. The quote currency is
`project.currency`; it is not repeated in `pricing`.

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

* `dxf_panel.hpp` — `buildDxfPanelItem(project, dxf, request, itemId, placement, &diags)`: a `formats::DxfImport`
  becomes a free `PanelItem` (Custom `ContourSpec`, circle / rectangle cut-outs), `pickDxfPanelMaterial`,
  `dxfPanelItemId` (slug, made unique). One rule set for `fos import-dxf` and the app; unsupported cut-outs →
  `project.import_dxf.cutout_unsupported` (Error, or Warning with `skipUnsupportedCutouts`).

## 4a. Component library (`fos/project/components.hpp`)

A **component** is one item (cabinet or free panel) plus copies of exactly the library entries it references
(`libraryReferences(item)`: every material of the assignment incl. drawer / drawer bottom, edge bands of the
assignment and part overrides, override materials, every `HardwareSelection` id and the slide of every drawer
box). It is self-contained and can be inserted into any project.

```cpp
auto c = makeComponent(project, "cab1", {.name = "Base 600", .category = "Base cabinets", .tags = {"kitchen"}});
saveComponents("base.foscomp", {c.value()});                 // atomic, checks it loads back
auto list = loadComponents("base.foscomp");                  // errors name the file + JSON pointer
auto plan = planComponentInsert(project, c, formats::ConflictPolicy::RenameWithNewId, componentIdGenerator("k1"));
doc.execute(insertComponent(c, "k1", geometry::Transform::translation({1200, 0, 0}), formats::ConflictPolicy::Skip));
ComponentLibrary lib; auto notes = lib.scan({userDir, sharedDir});   // byCategory / byTag / find / categories / tags
```

* `makeComponent` defaults: name = item name, id = `comp.<slug>`, preview size = generated bounding box
  (x = width, z = height, y = depth). The item is stored with identity placement.
* `insertComponent` (undoable `Command`) merges the embedded entries with `formats::mergeLibrary` semantics
  — `Skip` keeps the project's entry, `Replace` overwrites it in place (affects every item that uses it),
  `RenameWithNewId` adds the incoming entry as `<item-id>.lib1`, `.lib2`, ... and rewrites the new item's
  references — except that an entry **identical** (same JSON) to the project's entry with the same id is always
  kept (reported `Skipped`), so built-in hardware is never duplicated. The item gets the new id (cabinet part ids
  derive from it; a panel's part id becomes `<item-id>/panel`), the component name and the placement, and must
  generate, otherwise nothing changes.
* `ComponentLibrary` order is (category, name, id, source), byte-wise. Unreadable files are Error diagnostics
  (`project.components.load_failed`), duplicate ids Warnings (`project.components.duplicate_id`, first wins),
  missing paths Warnings. Plugin components are added with `add(componentFromRecord(record, path), source)`.

### Component files (`.foscomp`, container kind `furnitureos-component`, version 1)

Container as in formats README §1. Payload (the payload root and every component object are *extensible*:
unknown keys are preserved; `previewSize` is closed):

```json
{ "components": [ {
    "id": "comp.base-600",            // required, non-empty, unique in the file (plugins: "<plugin-id>:<local>")
    "name": "Base 600",               // required, non-empty, unique in the file
    "description": "", "category": "Base cabinets", "author": "",   // optional strings
    "tags": ["kitchen", "base"],      // optional; non-empty, unique strings
    "previewSize": {"width": 600, "height": 820, "depth": 560},     // mm, >= 0, 0 = unknown; preview only
    "library": {"materials": [...], "edgeBands": [...], "hardware": [...]},  // .foslib payload (formats codec)
    "definition": { "type": "cabinet", ... }   // an item object exactly as in section 2 (toJson(Item))
} ] }
```

Rules: at least one component; the `library` must contain every id the definition references (error
`/payload/components/<i>/library: component "<id>" references material "<x>" which is not embedded ...`);
`definition` is decoded strictly with `itemFromJson` (errors point into `/payload/components/<i>/definition/...`).
The formats module (`fos/formats/component.hpp`) checks the container, metadata, library and the definition's
`type`; the project module decodes the definition.

## 4b. Production batches (`fos/project/batch.hpp`)

Several projects (orders) produced together. `loadBatch(path)` loads the `.fosbatch` document and
then each listed project ON ITS OWN (paths relative to the batch file directory): a missing or
broken project is an Error `batch.project.load_failed` of that project and never aborts the
others. `mergeBatch()` builds one `domain::Model`:

* project key = entry `key`, or the file stem made file-name safe (`[A-Za-z0-9._-]`, others `_`);
  two entries with the same effective key are refused (set `key` on one of them);
* instance key = `<key>` for quantity 1, `<key>#<n>` (n = 1..K) for quantity K > 1; every
  assembly, part and hardware-instance id becomes `<instance key>/<original id>` (hardware part
  references follow). A project listed with quantity 3 contributes three copies, so cut-list
  quantities, hardware and edge banding are multiplied;
* libraries (materials, edge bands, hardware definitions) are merged by id; the same id with
  different data (compared via the formats codec) is an Error `batch.library.conflict` that names
  both projects, and the LATER project is left out (no part is ever cut from guessed data);
* `materialSubstitutions` (`"a": "b"`) explicitly re-assigns every part made of `a` to stock `b`
  (e.g. two library ids for the same board). Never silent: Info `batch.substitution.applied` per
  affected project with the part count; a target missing from all libraries
  (`batch.substitution.unknown_target`) or of a different thickness
  (`batch.substitution.thickness_mismatch`) is an Error and the substitution is not applied.
  Chains (`a`->`b`, `b`->`c`) are refused on load.

Outputs reuse the project glue on the merged model (`productionReports(model)`,
`cuttingPlans(model, settings)` are the shared overloads): `batchCutList`, `batchCuttingPlans`
(guillotine + nesting with the batch `cutting` settings; both HEURISTICS, validated
independently), `batchLabels` (label project text `"<batch> | <project> | order <instance key>"`),
`batchSummary` / `batchSummaryTable` (per project and total: parts, sheets AREA LOWER BOUND,
edge banding applied/purchase metres, hardware quantity; the total is computed on the merged
model and can be lower than the sum of the project rows because orders share sheets).

### Batch files (`.fosbatch`, container kind `furnitureos-batch`, version 1)

Same envelope as `.fosp` (checksum, versioning policy; version 1 has no migrations). Payload root
is extensible (unknown keys preserved); all other objects are closed.

| key | type | meaning |
|---|---|---|
| `batch` | object, required | `{"name", "date", "notes"}` strings (date by convention `YYYY-MM-DD`) |
| `projects` | array (≤ 200) | `{"path" (required, '/'-separated, relative to the batch file; absolute accepted), "quantity" (1..500, default 1), "key"? ([A-Za-z0-9._-]+), "note"?}` |
| `cutting` | object | exactly the project `cutting` object (2.6: `defaults`, `materials`, `nesting`?) |
| `edgeBanding` | object | `{"overhangPerEnd" ≥ 0 (mm, default 25), "wastePercent" ≥ 0 (default 0)}` |
| `cutListGroupAcrossAssemblies` | bool | default true |
| `materialSubstitutions` | object | material id -> material id (see above) |

The quantities of all entries add up to at most 1000 copies.

## 4c. Worktops (`fos/project/worktop.hpp`)

`planWorktop(project, WorktopRequest)` / `addWorktop(project, request)` / undoable `commands::addWorktop(request)`
build a kitchen worktop over a straight run of CABINET items as one or more free panel items (role `Worktop`,
Horizontal, part id `<item>/panel`). Nothing new is stored in the file: pieces are ordinary panel items whose
parametric contour holds the cut-outs, so cutting/nesting, CAM, DXF export and drawings handle them unchanged.

* **Run.** The first listed cabinet is the reference frame (the pieces get its placement); every cabinet must share
  its rotation, must not overlap a neighbour (> 0.5 mm) and the tops must be level within 1 mm. Gaps are bridged
  (`project.worktop.gap` warning). Extents are the bounding boxes of the GENERATED parts (doors included).
* **Size.** x = run ± side overhangs, front edge = foremost part − front overhang (default 20), back edge = cabinet
  backs − back gap (default 0), underside = highest top, thickness = material thickness. No material named →
  `mat.generic.worktop38` (38 mm, stock 4100 × 650, grain along the length, cost 0) is added to the library.
* **Edges.** Front (L1) banded with the chosen band (named, else the first library band by id that covers the
  thickness, else a generic `eb.generic.abs2x45` placeholder is added); back and free ends raw unless requested;
  joint ends always raw.
* **Joints.** Longer than the usable stock length (sheet length − cutting trims of the material, or
  `maxPieceLength`) → the fewest equal pieces (`<id>.1`, `<id>.2`, ...) with plain BUTT JOINTS (no mitre, connector
  bolts not modelled), each joint moved to the nearest position ≥ `minJointDistance` from every cut-out that keeps
  all pieces within the stock length; none → refused. `project.worktop.butt_joint` notes name each joint.
* **Cut-outs** (sink / hob): rectangle or rounded rectangle, centre x measured from the left face of the leftmost
  cabinet, centred in depth unless a front setback is given. `WorktopRules` defaults are RULES OF THUMB (not a
  standard, check the appliance sheet): 50 mm to front/back edge, 100 mm to a free end, 150 mm to a joint, 100 mm
  between cut-outs. A violation refuses the request with the distances in the message.

## 4d. Plinths (`fos/project/plinth.hpp`)

`planPlinth(project, PlinthRequest)` / `addPlinth(project, request)` / undoable `commands::addPlinth(request)` add a
kitchen plinth (toe-kick) under a straight run of cabinets as free panel items (role `Plinth`, part id
`<item>/panel`). The run analysis (reference frame, rotation, overlap, gaps) is shared with the worktop builder
(private `src/cabinet_run.hpp`).

* **Cabinets.** Ordinary cabinets must stand on feet (`PlinthKind::Feet`); SidesToFloor (own recessed plinth panel,
  sides to the floor) and None are refused. Appliance gaps (dishwasher, any plinth kind) are accepted: their end
  panels reach the floor, so the plinth is interrupted there and the strip in front of the appliance becomes its
  own piece (`project.plinth.appliance` note). All cabinets must stand on the same floor level (1 mm).
* **Size.** Height = `height`, else the common `plinth_height` of the Feet cabinets (within 1 mm; an explicit height
  may not exceed it). Front face = foremost carcass front (y = 0 of the cabinet frames) + `setback` (default 50 mm,
  a RULE OF THUMB). Length = run of the generated parts minus the appliance end panels; gaps are bridged
  (`project.plinth.gap` warning). Material: named, else the first listed cabinet's carcass material. All edges raw.
* **Joints.** A segment longer than the usable stock length (sheet length − trims, or `maxPieceLength`) is split
  into the fewest equal pieces (`<id>.1`, ...) with plain BUTT JOINTS (`project.plinth.butt_joint`); plinth clips and
  connectors are not modelled.
* **Returns** (`returnLeft` / `returnRight`, items `<id>.left` / `<id>.right`): VerticalSide panels flush with the
  outer face of the end cabinet, from the back face of the front plinth to the back of that cabinet; refused at an
  end closed by an appliance end panel.
* Rule of thumb (note only): integrated dishwashers often need a lower or notched plinth for the door swing.

## 5. Limitations

* The per-project parts limit is checked on the estimate (`estimatePartCount`), an upper bound; custom parameter
  values are not limited (sizes derived from them are checked on the generated parts).
* Worktops: straight runs only (no L/U corner mitres), rectangular / rounded-rectangle cut-outs only, no
  connector-bolt pockets, no wall-profile scribing; a worktop does not follow later cabinet edits (rebuild it).
* Plinths: straight runs only (no corner mitres), equal splits only (joints are not aligned with the feet), no
  clips / feet fixing, no notch for appliance doors, no edge banding; a plinth does not follow cabinet edits.
* Rejecting a file at the 500-item limit still parses the whole (≤ 256 MiB) JSON text first.

* Undo snapshots cost memory proportional to the project size per step (bounded by the undo limit).
* Labels carry item ids, not display names.
* Components: no thumbnails (only `previewSize`), no versioning or update of inserted components (an inserted
  item does not remember its source component), drawing edits are not stored in a component.
* `recoverProject` returns the newest autosave/backup that passes the container checks; if its payload then fails
  to decode the error is returned (older candidates are not tried).
* No `.fosp` migrations exist yet (version 1); the migration path is exercised only by the formats module tests.
* The checksum is an integrity check, not a signature (formats README §1.1).

## 6. Tests

`fos_project_tests` (`plinth_tests.cpp`: height from the feet, setback, 3 equal pieces + returns, interruption at a
dishwasher end panel, refusals, undo; `worktop_tests.cpp`: run extent and overhangs in the reference frame, generic material/band,
split at 2400 mm, joint moved from 2400 to 1850 by a sink keep-out, cut-out position in the second piece, rule-of-thumb
refusals, run errors, undoable command): default libraries; base-cabinet part and cut sizes (hand-computed); panel items and
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
