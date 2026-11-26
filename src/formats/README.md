<!-- SPDX-License-Identifier: AGPL-3.0-only -->
# `formats` — persistence and interoperability

Module `fos::formats` (depends on `core`, `geometry`, `domain`; third-party: nlohmann/json ≥ 3.11).
This file is the public documentation of the FurnitureOS file formats handled here.

| Area | Header | What it does |
|---|---|---|
| Container | `container.hpp` | Versioned JSON envelope, checksum, load pipeline, migrations |
| Saving | `atomic_file.hpp` | Atomic replace, rotating backups, autosave file |
| Recovery | `recovery.hpp` | List backups/autosaves, load the newest valid one |
| Codecs | `codec.hpp`, `enums.hpp` | JSON ⇄ `domain` types, strict validation |
| Libraries | `library.hpp` | `.foslib` import / export / merge |
| OBJ | `obj_export.hpp` | Wavefront OBJ + MTL export (write-only) |
| CSV | `csv_materials.hpp` | Material list import (read-only) |
| DXF | `dxf_import.hpp` | 2D outline import (ASCII DXF) for panels (read-only) |
| Hash | `sha256.hpp` | SHA-256 (own implementation, FIPS 180-4) |

Conventions: all lengths are millimetres, angles do not occur, text is UTF-8. Expected failures are returned as
`core::Result` with an English message; nothing here throws or aborts on a bad file.

---

## 1. Container

Every native file (`.fosp` project, `.foslib` library) is one UTF-8 JSON object, written with 2-space
indentation and a trailing newline:

```json
{
  "format": "furnitureos-library",
  "formatVersion": 1,
  "app": { "name": "FurnitureOS", "version": "0.1.0" },
  "savedAt": "2026-01-31T12:00:00Z",
  "checksum": { "algorithm": "sha256", "value": "<64 lower-case hex digits>" },
  "payload": { }
}
```

| Key | Type | Required on load | Meaning |
|---|---|---|---|
| `format` | string | yes | Kind of document. Must equal the kind the reader expects. |
| `formatVersion` | integer ≥ 1 | yes | Version of the **payload** schema of this kind. |
| `app` | object `{name, version}` | no | Application that wrote the file. Informational; rewritten on every save. |
| `savedAt` | string | no | ISO-8601 UTC time `YYYY-MM-DDTHH:MM:SSZ` of the save. |
| `checksum` | object | yes (see below) | Integrity check of `payload`. Only `"sha256"` exists. |
| `payload` | object | yes | The document. Opaque to the container. |
| anything else | any | no | Unknown envelope keys are kept (`Document::extra`) and written back after `payload`. |

The container never interprets, reorders or drops anything inside `payload`: every key at every depth survives
load → save, and key order is preserved, so re-saving an unmodified file reproduces it byte for byte (given the
same `app` and `savedAt`).

### 1.1 Checksum canonicalisation

`checksum.value` = lower-case hex SHA-256 of the **canonical text** of `payload`:

1. object keys sorted by byte-wise comparison of their UTF-8 names, at every nesting level;
2. no whitespace between tokens (`{"a":1,"b":[1,2]}`);
3. strings as UTF-8 with only the escapes JSON requires (`\"`, `\\`, control characters as `\n`, `\t`, `\u00XX` …);
   non-ASCII characters are **not** `\u`-escaped;
4. numbers as nlohmann/json prints them: integers without fraction or exponent; floating-point values with the
   shortest decimal text that reads back as the same IEEE-754 double, always containing `.` or an exponent
   (`2.5`, `18.0`, `1e-07`);
5. array order is significant and unchanged.

Consequences: reformatting a file or reordering object keys does not invalidate the checksum; changing any value
does. A third-party tool can reproduce the value with any JSON library that offers sorted-key compact output and
shortest round-trip number formatting. The checksum detects damage and accidental edits; it is **not** a
signature and offers no protection against deliberate tampering.

### 1.2 Load pipeline (`loadDocument` / `parseDocument`)

Checks run in this order; the first failure is returned and nothing is loaded partially.

| # | Condition | `ErrorCode` | Message contains |
|---|---|---|---|
| 1 | File missing, a directory, not a regular file (FIFO, character/block device, socket — refused before it is opened for reading, so a pipe or `/dev/zero` never blocks or exhausts memory), unreadable | `IoError` | path, what it is |
| 1a | File larger than `LoadOptions::maxFileBytes` (default `kDefaultMaxFileBytes` = 256 MiB), checked on the size before reading and enforced while reading | `ConstraintViolation` | size and limit in bytes |
| 2 | Empty, not JSON, nesting deeper than 128 levels | `ParseError` | `line L, column C` |
| 3 | Top level not an object; `format` missing or not a string | `CorruptData` | what was found |
| 4 | `format` is another kind | `Unsupported` | both kinds |
| 5 | `formatVersion` missing / not an integer / < 1 | `CorruptData` | value |
| 6 | `formatVersion` newer than supported | `UnsupportedVersion` | file version N, supported version M, and the writing application's version if the file records one (otherwise the message says it is not recorded) |
| 7 | `payload` missing or not an object | `CorruptData` | |
| 8 | `checksum` missing, malformed, unknown algorithm, or mismatch | `CorruptData` | stored and actual hash |
| 9 | A migration step is missing | `UnsupportedVersion` | the missing step |
| 10 | A migration step failed or threw | the step's code / `CorruptData` | `Migration N -> N+1 failed` |

**Recovery flag.** `LoadOptions::allowChecksumMismatch = true` turns failure 8 (and only 8) into a `Warning`
diagnostic (`formats.container.checksum_mismatch` or `formats.container.checksum_missing`) and returns the payload
as found. It is an explicit opt-in for "open anyway" after the user was told the file is damaged; the default is
to refuse. A file newer than supported is refused even with this flag.

### 1.3 Versioning and migration policy

* `formatVersion` is a single integer per kind, starting at 1. It is increased when a reader of the previous
  version would misinterpret or reject the payload. Purely additive optional keys do not require a new version.
* A reader supports exactly the versions `1 … currentVersion` (`FormatSpec`). Newer files are refused; the
  application never guesses.
* Older files are upgraded in memory by a chain of steps `N → N+1`
  (`MigrationRegistry::add(N, Result<void>(Json& payload), description)`). Steps run sequentially; the chain is
  checked for completeness before the first step runs; the payload is replaced only when every step succeeded.
  `LoadedDocument::migrations` lists the steps that ran, `fileVersion` the version found in the file. The file on
  disk changes only when the user saves (and the old version then becomes `.bak1`).
* Migration steps must keep keys they do not know.
* There is no downgrade path.

Registered kinds in this module: `furnitureos-library` version 1 (no migrations yet). The `.fosp` kind
`furnitureos-project` and its migrations are defined by the `project` module on top of this container.

---

## 2. Atomic save and backups

`saveDocument(path, document, options)` (and the generic `atomicWriteFile`) performs:

1. **WriteTemp** — the complete content is written to `<name>.tmp-<random>` in the *same directory*, flushed and
   synced to the device (`fsync` on POSIX; `_commit` on Windows).
2. **Verify** — the temporary file is read back and compared byte for byte; for documents it is additionally
   parsed and its checksum recomputed and compared.
3. **RotateBackups** — only if the target exists and `backupCount` N > 0: `.bak(N-1)` → `.bakN`, …, `.bak1` →
   `.bak2`, then the current file becomes `<name>.bak1` via a hard link (or a copy where hard links are not
   available). The target itself stays in place during the whole step. The previous `.bakN` is deleted only after
   step 4 succeeded.
4. **Replace** — the temporary file is renamed over the target (atomic on POSIX and NTFS); on POSIX the directory
   is then synced.

Guarantees:

* At every instant `path` holds either the complete old file or the complete new file.
* If any stage fails, the old file, **and all backups**, are exactly as before; the temporary file is removed; the
  error names the stage (`... failed (Verify stage); the existing file was left unchanged. ...`).
* `backupCount` (default 3, at most `kMaxBackupCount` = 100; anything outside 0 … 100 is an InvalidArgument error
  before anything is written): `0` creates no backups and leaves existing `.bak` files alone. `.bak1` is always the
  most recent previous version. Lowering the count later does not delete higher-numbered old backups.
* Existing backups are found with **one listing of the target's directory** (names `<name>.bakN`, N = 1 … count
  without leading zeros); only those files are renamed. A save therefore costs O(entries in the directory), not
  one file-system probe per possible index.
* `savedAt` comes from `Document::savedAt`, or — when that is empty — from `SaveOptions::clock`
  (default: system clock), which makes output reproducible in tests.

Known limits: after a power loss a stale `<name>.tmp-*` file may remain (it is never read and can be deleted). On
Windows the directory entry itself cannot be flushed, so the rename may be lost on power failure — the old file
then remains. Two processes saving the same path at once are not coordinated.

`SaveOptions::writer` and `SaveOptions::beforeStage` are fault-injection points used by the tests.

### Autosave and recovery

| File | Written by | Purpose |
|---|---|---|
| `<name>.autosave` | `writeAutosave(path, document)` | Unsaved state; never touches `<name>` or the backups. `discardAutosave(path)` deletes it. |
| `<name>.bak1 … .bakN` | `saveDocument` | Previous saved versions, 1 = newest. |

Scheduling autosaves is the application's job. `listRecoveryCandidates(path)` returns the existing autosave and
backup files, newest first by modification time (ties: autosave first, then lower backup index).
`loadNewestValid(path, spec)` returns the first one that passes the full load pipeline, together with the list of
newer candidates that were rejected and why; `NotFound` if there is none.

---

## 3. Codec schemas

`toJson(x)` writes every key listed below (except an empty `custom`). `…FromJson(json, path)` validates strictly;
errors are `ParseError` with a JSON-pointer-like location:

```
/assemblies/0/parts/3/length: expected a positive number, got -5
/materials/0/kind: expected one of "particleboard", "mdf", "hdf", "plywood", "solid_wood", "other", got "MDF"
```

Legend — **R**: required key. Other keys are optional on load and take the stated default. "positive" = finite
and > 0; "non-negative" = finite and ≥ 0. Numbers may be written as JSON integers or decimals. Ids are non-empty
strings. Doubles round-trip exactly.

**Unknown keys.** Types marked *(custom)* keep an unknown key `k` in the entity's `custom` map as
`custom["json:k"] = <compact JSON text>` and write it back as top-level key `k` — they are preserved, in memory
and on disk. The prefix `json:` is reserved. A `json:` entry that cannot be written back (text is not JSON, or
`k` is a known key) is stored as an ordinary `custom` property instead. For all other types an unknown key is an
**error** naming the key and the allowed keys; it is never dropped silently.

### 3.1 Enumeration tokens

Stable lower-case strings; independent of the display names in `domain::toString`. Matching is case-sensitive.

| Enumeration | Tokens |
|---|---|
| `MaterialKind` | `particleboard`, `mdf`, `hdf`, `plywood`, `solid_wood`, `other` |
| `EdgeBandKind` | `abs`, `pvc`, `melamine`, `veneer`, `solid_wood`, `other` |
| `HardwareKind` | `hinge`, `mounting_plate`, `drawer_slide`, `handle`, `shelf_support`, `confirmat`, `dowel`, `cam_connector`, `screw`, `foot`, `leg`, `suspension`, `sliding_system`, `lift_mechanism`, `other` |
| `PartRole` | `side`, `top`, `bottom`, `shelf`, `partition`, `back`, `door`, `drawer_front`, `drawer_side`, `drawer_back`, `drawer_bottom`, `plinth`, `worktop`, `rail`, `generic` |
| `EdgeSide` | `l1`, `l2`, `w1`, `w2` |
| `GrainDirection` | `none`, `along_length`, `along_width` |
| `Face` | `a`, `b`, `l1`, `l2`, `w1`, `w2` |

### 3.2 Color
A string `"#RRGGBB"`: `#` and exactly six hexadecimal digits (either case on load, upper case on save). sRGB,
display only.

### 3.3 Placement (`geometry::Transform`) — unknown keys rejected

| Key | Type | | Meaning |
|---|---|---|---|
| `origin` | `[x, y, z]` | R | Child-frame origin in the parent frame, mm |
| `xAxis`, `yAxis`, `zAxis` | `[x, y, z]` | R | Child axes expressed in the parent frame |

The axes must form a proper rotation (`Transform::isRigid`, tolerance 1e-9): unit length, mutually perpendicular,
right-handed (`zAxis = xAxis × yAxis`). Mirrored, scaled or skewed frames are rejected.

### 3.4 Material *(custom)*

| Key | Type | | Default | Rule |
|---|---|---|---|---|
| `id` | string | R | | non-empty |
| `name` | string | R | | |
| `manufacturer`, `code`, `supplier`, `texture` | string | | `""` | |
| `kind` | MaterialKind | | `particleboard` | |
| `thickness` | number | R | | positive, mm |
| `sheetLength` | number | | 2800 | positive, mm (along the grain) |
| `sheetWidth` | number | | 2070 | positive, mm |
| `hasGrain` | boolean | | `false` | |
| `color` | Color | | `#C8C8C8` | |
| `costPerSheet` | number | | 0 | non-negative; 0 = unknown |
| `costPerM2` | number | | 0 | optional, written only when > 0; non-negative; 0 = unknown |
| `density` | number | | 0 | kg/m³; optional, written only when > 0; 0 = unknown (packing weight unknown) |
| `available` | boolean | | `true` | |
| `custom` | object string→string | | `{}` | user-defined properties |

### 3.5 EdgeBand *(custom)*

| Key | Type | | Default | Rule |
|---|---|---|---|---|
| `id` | string | R | | non-empty |
| `name` | string | R | | |
| `manufacturer`, `code` | string | | `""` | |
| `kind` | EdgeBandKind | | `abs` | |
| `thickness` | number | R | | positive, mm |
| `width` | number | R | | positive, mm |
| `color` | Color | | `#C8C8C8` | |
| `costPerMeter` | number | | 0 | non-negative |
| `custom` | object string→string | | `{}` | |

### 3.6 HardwareDefinition *(custom)*

| Key | Type | | Default | Rule |
|---|---|---|---|---|
| `id` | string | R | | non-empty |
| `kind` | HardwareKind | R | | |
| `name` | string | R | | |
| `manufacturer`, `article` | string | | `""` | |
| `dimensionsVerified` | boolean | | `false` | |
| `unitCost` | number | | 0 | non-negative |
| `params` | object string→number | | `{}` | finite numbers, mm |
| `custom` | object string→string | | `{}` | |

### 3.7 HardwareInstance — unknown keys rejected

| Key | Type | | Default | Rule |
|---|---|---|---|---|
| `id` | string | R | | non-empty |
| `definition` | string | R | | non-empty HardwareDefinition id |
| `quantity` | integer | | 1 | ≥ 1 |
| `parts` | array of string | | `[]` | non-empty part ids |
| `position` | `[x, y, z]` | | `[0, 0, 0]` | assembly frame, mm |
| `note` | string | | `""` | |
| `length` | number | | `0` | ≥ 0, mm per piece of a linear fitting (wall rail, track); written only when > 0 |

### 3.8 Operation — unknown keys rejected

Common keys: `id` (R, non-empty), `type` (R: `"hole"`, `"groove"` or `"pocket"`), `face` (R, Face),
`source` (string, default `"user"`), `note` (string, default `""`). Face coordinates `(u, v)` are defined in
`fos/domain/operation.hpp`. Keys of another shape are unknown keys.

| `type` | Key | Type | | Default | Rule |
|---|---|---|---|---|---|
| `hole` | `u`, `v` | number | R | | centre, mm |
| | `diameter` | number | R | | positive |
| | `depth` | number | | 0 | non-negative (ignored when `through`) |
| | `through` | boolean | | `false` | |
| `groove` | `u1`, `v1`, `u2`, `v2` | number | R | | centre line, mm |
| | `width` | number | R | | positive |
| | `depth` | number | R | | positive |
| `pocket` | `contour` | array of `[u, v]` | R | | at least 3 points, counter-clockwise |
| | `depth` | number | | 0 | non-negative |
| | `through` | boolean | | `false` | |
| | `cornerRadius` | number | | 0 | non-negative, mm; inner corner radius. Written only when > 0, so files without radii are unchanged |

### 3.9 Part *(custom)*

| Key | Type | | Default | Rule |
|---|---|---|---|---|
| `id` | string | R | | non-empty |
| `name` | string | R | | |
| `role` | PartRole | | `generic` | |
| `length`, `width`, `thickness` | number | R | | positive, mm; finished size |
| `material` | string | | `""` | Material id; empty = unassigned |
| `edges` | object | | `{}` | keys `l1`, `l2`, `w1`, `w2`; value = EdgeBand id, or `null` / absent = raw edge |
| `grain` | GrainDirection | | `none` | |
| `placement` | Placement | | identity | part frame → assembly frame |
| `operations` | array of Operation | | `[]` | ids unique within the part |
| `shape` | PartShape (3.9.1) | | absent | contoured part (ADR-0008); **omitted** for rectangular parts, which are therefore written exactly as before shapes existed. With a shape, `edges` must be empty (explicit `null`s allowed). Written after `operations`, before `custom` |
| `custom` | object string→string | | `{}` | |

A preserved unknown-key entry `custom["json:shape"]` (from an older reader) is never turned back into a
`shape` key: it stays an ordinary custom property.

#### 3.9.1 PartShape — unknown keys rejected

Lengths in mm in the part frame's XY plane. A *contour* is an array of vertices; segment i runs from vertex i to
vertex i+1 (closing back to vertex 0; the first vertex is not repeated). DXF LWPOLYLINE bulge convention
(`fos/geometry/contour.hpp`): `bulge = tan(θ/4)`, 0 = line, > 0 counter-clockwise arc, ±1 = half circle.

```json
"shape": {
  "outer":   [{"x": 0.0, "y": 0.0}, {"x": 800.0, "y": 0.0}, {"x": 800.0, "y": 600.0, "bulge": 0.25}, {"x": 0.0, "y": 600.0}],
  "cutouts": [[{"x": 1030.0, "y": 300.0, "bulge": 1.0}, {"x": 970.0, "y": 300.0, "bulge": 1.0}]],
  "outerEdges": ["eb.abs2", null, null, null]
}
```

| Key | Type | | Default | Rule |
|---|---|---|---|---|
| `outer` | contour | R | | finished outline (edge banding included) |
| `cutouts` | array of contour | | `[]` | through openings, never banded |
| `outerEdges` | array of (EdgeBand id \| `null`) | | `[]` | `[]` = all segments raw, else exactly one entry per outer segment; `null` = raw |
| vertex `x`, `y` | number | R | | finite |
| vertex `bulge` | number | | 0 | finite; **written only when ≠ 0**, optional on load (an explicit `0` is accepted and dropped on the next save) |

Vertices are objects with exactly these keys (others are errors). Writing always emits all three shape keys
(`cutouts` / `outerEdges` as `[]` when empty), so encode → decode → encode is byte-identical.

Validation on load (first failure wins; every message starts with the JSON pointer, e.g.
`/assemblies/0/parts/2/shape/cutouts/1: ...`):

| Check | Location |
|---|---|
| `edges` has a band although a shape exists | `…/edges/<side>` |
| > 10 000 vertices in one contour (`kMaxContourVertices`), > 1 000 cut-outs (`kMaxShapeCutouts`), > 20 000 vertices in outer + all cut-outs (`kMaxShapeVertices`), > 10 000 `outerEdges` entries — checked before the vertices are read | `…/shape/outer`, `…/shape/cutouts[/k]`, `…/shape/outerEdges` |
| `geometry::validateContour` fails (≥ 3 vertices, or 2 with an arc; finite; \|bulge\| < 1e6; no zero-length segment; no self-intersection) — its message is appended | `…/shape/outer`, `…/shape/cutouts/k` |
| outer contour clockwise (signed area ≤ 0) | `…/shape/outer` |
| exact bounding box of `outer` (arc extrema included) ≠ `[0, length] × [0, width]` by more than 1e-6 mm (`kShapeBoundsTolerance`); the message gives both boxes | `…/shape/outer` |
| a cut-out vertex outside or on (within 1e-6 mm) the outer contour (exact point location, arcs not approximated) | `…/shape/cutouts/k/i` |
| a cut-out segment touching or crossing an outer segment (exact line/arc intersection, eps 1e-6 mm) | `…/shape/cutouts/k` |
| two cut-outs touching/overlapping, or one nested inside another | `…/shape/cutouts/m` (the later one) |
| `outerEdges` neither empty nor one entry per outer segment; an empty id | `…/shape/outerEdges[/i]` |

Cut-outs may be written in either orientation; it is kept as written. The limits bound the cost of
`validateContour`, which is O(n²) per contour (about 0.9 s for 10 000 segments in an optimised build).

### 3.10 Assembly — unknown keys rejected

| Key | Type | | Default | Rule |
|---|---|---|---|---|
| `id` | string | R | | non-empty |
| `name` | string | R | | |
| `placement` | Placement | | identity | assembly frame → project frame |
| `parts` | array of Part | | `[]` | ids unique within the assembly |
| `hardware` | array of HardwareInstance | | `[]` | ids unique within the assembly |

### 3.11 Model — unknown keys rejected

| Key | Type | Default | Rule |
|---|---|---|---|
| `materials` | array of Material | `[]` | unique ids |
| `edgeBands` | array of EdgeBand | `[]` | unique ids |
| `hardwareCatalog` | array of HardwareDefinition | `[]` | unique ids |
| `assemblies` | array of Assembly | `[]` | unique ids |

The codecs validate each value and id uniqueness. They do **not** check cross-references (a part naming a
material that is not in the model decodes fine); that is the `validation` module's job.

`modelToJsonChecked(model)` encodes and immediately decodes the result, and fails if the model contains something
the format cannot represent or would reject on load (NaN/∞, non-positive sizes, non-rigid placements, duplicate
ids, out-of-range enumerators). Use it before saving so that a file that cannot be read back is never written.
Plain `toJson` never fails; a non-finite number would be written as `null`.

---

## 4. Library files (`.foslib`)

Container kind `furnitureos-library`, `formatVersion` 1. Payload:

| Key | Type | Rule |
|---|---|---|
| `materials` | array of Material (3.4) | unique ids |
| `edgeBands` | array of EdgeBand (3.5) | unique ids |
| `hardware` | array of HardwareDefinition (3.6) | unique ids |
| other keys | any | preserved (`Library::extra`) and written back |

`importLibrary(path, options)` reads the file with `readFile` (limit `options.maxFileBytes`) and parses it with
`importLibraryText(text, sourceName, options)`, which callers that already hold the bytes (e.g. the plugin loader,
which reads each file exactly once through its own bounded reader) use directly; it refuses text longer than
`options.maxFileBytes` (`ConstraintViolation`).

All three arrays are optional on load (default empty). A duplicate id inside one list of one file is an error on
import (`/payload/edgeBands/2/id: duplicate id "eb.abs2" (already used by /payload/edgeBands/0)`) and on export
(nothing is written). The same id text may occur in different lists.

**Merge** (`mergeLibrary(target, incoming, policy, newId)`): entries whose id is new are appended; for an id that
already exists in the same list of the target the policy decides:

| Policy | Effect | Report action |
|---|---|---|
| `Skip` | existing entry kept, incoming ignored | `Skipped` |
| `Replace` | existing entry overwritten in place | `Replaced` |
| `RenameWithNewId` | incoming entry added under a new id (default: UUID v4) | `Renamed` (`incomingId` → `resultId`) |

The report has one entry per incoming item (category, action, incoming id, resulting id, name). The merge is
all-or-nothing. Renaming does not rewrite references to the old id elsewhere; callers remap with the report.
Two entries with different ids but identical content are not detected as duplicates.

### 4.1 CAM tool tables (`.fostools`, target `fos::formats_cam`)

`fos/formats/tool_table.hpp`, sources `src/tool_table.cpp`, `src/tool_table_check.cpp`. A separate
target linking `fos::formats` + `fos::cam`, created only when `cam` is part of the build, so that
`fos::formats` stays free of CAM. Container kind `furnitureos-tools`, version 1 (no migrations),
checksum as for every container. Payload:

```json
{ "tools": [
  { "id": "mill.6", "number": 12, "name": "6 mm upcut", "type": "flat_end_mill",
    "diameter": 6, "fluteLength": 22, "overallLength": 60, "maxDepthPerPass": 3,
    "centerCutting": true, "spindleRpm": 18000, "feedRate": 3000, "plungeRate": 1000,
    "notes": "free text" } ] }
```

* Required: `id`, `number` (integer), `type` (`drill` | `flat_end_mill`), `diameter`, `fluteLength`.
  Optional: `name`, `overallLength`, `maxDepthPerPass`, `centerCutting` (end mills, default true),
  `spindleRpm`, `feedRate`, `plungeRate`, `notes`. Unknown keys in a tool are refused; other payload keys
  are kept (`ToolTable::extra`).
* `toolTableFromJson`: structural errors (first one, ParseError, JSON path in message and context).
* `validateToolTable`: all value findings as diagnostics with the JSON path as entity, e.g.
  `/payload/tools/1/maxDepthPerPass: tool 'mill.6' has max depth per pass 25 mm, more than its flute
  length 22 mm.` Rules: at least one tool; unique ids and numbers; number >= 1; diameter, flute length,
  overall length, max depth per pass, rpm and feeds > 0; overall length >= flute length; max depth per
  pass <= flute length and required for flat end mills; on a drill it is unused (warning).
* `saveToolTable` refuses an invalid table; `loadToolTable` / `parseToolTableText` fail on container or
  structural errors and return value findings as diagnostics (check `core::hasErrors`).
* `toToolLibrary` / `toolTableFromLibrary` convert to / from `cam::ToolLibrary`; CAM needs rpm, feed and
  plunge rate on every tool. Tests: `tests/tool_table_tests.cpp` (`fos_formats_cam_tests`).

---

## 5. Exchange formats

### 5.1 Wavefront OBJ (+ MTL) — export only

`exportObj(model)` → OBJ text, MTL text, warnings. `writeObjFiles(path, model)` writes `<name>.obj` and
`<name>.mtl` (whitespace in the MTL file name replaced by `_`).

| | |
|---|---|
| Exported | Every part of every assembly as a box of its **finished** size `length × width × thickness`, positioned in the **project frame** (`assembly.placement * part.placement`). One `o` and one `g` per part (named after the part id), 8 `v`, 6 `vn`, 6 quad faces `f v//vn` with outward counter-clockwise winding. |
| Shaped parts | A part with `shape` (ADR-0008) is exported as its **finished outline with cut-outs** extruded over `[0, thickness]`: arcs tessellated with a chord tolerance of `kObjChordTolerance` = 0.05 mm (contour vertices kept exactly), n outline points → 2n `v`; `vn` −Z, +Z and one per outline edge; bottom and top caps as triangles (ear clipping with holes joined by bridge edges, inside `formats`), one quad per outline edge; all outward counter-clockwise, the mesh closed (every edge shared by exactly two faces in opposite directions). The volume is T·(area of the tessellated outer − areas of the tessellated cut-outs), i.e. within T · 0.05 mm · (arc length) of the exact value. A shape failing the codec checks (3.9.1), or with more than 20 000 tessellated points, fails the export naming the part. One extra header comment line about shaped parts is written only when the model has one, so exports of rectangular models are byte-identical to earlier versions. |
| Units | Millimetres, stated in the file header comment. No scaling. Coordinates are written with full double precision. |
| Axes | FurnitureOS project frame: right-handed, **Z up**. No conversion to the Y-up convention some viewers assume. |
| Materials | `usemtl <material id>` per part; the MTL file has one `newmtl` per used material with the display colour as `Kd` (4 decimals), `d 1`, `illum 1`. A part with no or an unknown material gets a neutral grey material and a `formats.obj.material_missing` warning. |
| Names | Whitespace, control characters and `#` in ids are replaced by `_`; clashes get `_2`, `_3`, … |
| Not exported | Machining operations (holes, grooves, pockets, pocket cut-outs; shape cut-outs *are* exported), edge bands, hardware, textures / UVs, grain direction, assembly hierarchy, costs, custom properties. |
| Errors | A part with a non-positive or non-finite size, or a non-rigid part/assembly placement, fails the export with a message naming the part. |
| Import | Not supported. |

### 5.2 CSV material list — import only

`importMaterialsCsv(text, options)` / `importMaterialsCsvFile(path, options)` → `std::vector<domain::Material>`.

* **Encoding and lines**: UTF-8, optional byte-order mark, LF or CRLF. Blank lines are skipped. The first
  non-blank line is the header. Row numbers in messages are 1-based file line numbers.
* **Separator**: `;` or `,`. Auto-detected from the header: `;` if the header contains a semicolon outside
  quotes, otherwise `,`. Can be forced with `CsvMaterialImportOptions::separator`. Tabs are not supported.
* **Quoting**: a field may be enclosed in `"`; inside, `""` is a literal quote and the separator is ordinary
  text. A quoted field cannot contain a line break. Unquoted fields are trimmed of spaces and tabs.
* **Columns** are matched by header name, case-insensitively, in any order:

  | Header | Required | Value | Empty cell |
  |---|---|---|---|
  | `name` | yes | text | error |
  | `thickness` | yes | positive number, mm | error |
  | `sheetLength` | no | positive number, mm | 2800 |
  | `sheetWidth` | no | positive number, mm | 2070 |
  | `kind` | no | MaterialKind token; case-insensitive, spaces and `-` read as `_` | `other` |
  | `costPerSheet` | no | non-negative number | 0 |
  | `hasGrain` | no | `true`/`false`, `yes`/`no`, `1`/`0` (any case) | `false` |
  | `id` | no | text, unique in the file | generated (UUID v4 or `options.newId`) |
  | `manufacturer`, `code`, `supplier` | no | text | `""` |
  | any other header | no | text, stored as `custom[<header>]` | not stored |

* **Numbers**: optional sign, digits, and at most **one** decimal mark which may be `.` **or** `,`
  (`18`, `18.5`, `18,5`). No thousands separators (`1.800,5` and `2 800` are errors), no exponent, no units.
  With the `,` separator a decimal comma must be inside a quoted field (`"18,5"`); unquoted it splits the field
  and the row is reported with a field-count error that says so.
* **Errors**: the import is all-or-nothing. Every problem is listed, one per line, e.g.
  `row 4, column 'thickness': expected a positive number, got 'abc'.` (first 50 are shown).
* **Not supported**: export, colours, textures, availability, multi-line fields, other separators, edge band or
  hardware lists.

### 5.3 DXF outline import — import only (`dxf_import.hpp`)

`importDxf(text, options)` / `importDxfFile(path, options)` read 2D outlines from **ASCII DXF** (R12 through
current; the publicly documented group-code format), `importPanelShape(loops)` turns them into a panel outline.

* **Entities**: `LINE`, `ARC`, `CIRCLE`, `LWPOLYLINE` (bulges, closed flag 70 bit 1), 2D `POLYLINE` / `VERTEX` /
  `SEQEND` (bulges; spline-frame control vertices are skipped, the fitted vertices used). Z is ignored. Entities
  with extrusion (0,0,-1) (mirrored OCS) are mirrored back into world XY; other extrusions are reported.
* **Not supported (reported as `formats.dxf.*` warnings, never dropped silently)**: `INSERT` / blocks
  (`insert_unsupported`: explode them first), `SPLINE`, `ELLIPSE`, `HATCH`, text, dimensions, 3D polylines,
  meshes and every other entity (`entity_unsupported`, one line per type with count and first line). Binary DXF
  is an error. The `BLOCKS`, `TABLES`, `OBJECTS` sections are skipped.
* **Units**: `$INSUNITS` 1 in (25.4), 2 ft (304.8), 4 mm, 5 cm, 6 m; absent or 0 = mm with warning
  `units_assumed`; other codes are an error unless `options.units` overrides the file. Output is always mm.
* **Layers**: `options.layer` keeps only that layer (case-insensitive); the skipped count is an Info.
* **Chaining**: closed polylines and circles are loops directly; `LINE`, `ARC` and open polylines are joined end to
  end when endpoints are within `chainTolerance` (default 0.01 mm, (0, 10]); several candidates = nearest one plus
  warning `ambiguous_junction`. Chains that do not close are reported with their end points and gap
  (`open_chain`). Collinear straight pieces are merged; arcs over 180 degrees and bulges > 1 are split, so every
  output bulge is within [-1, 1]. Loops that self-intersect are reported (`invalid_loop`). Output loops are CCW.
* **Panel shape**: the largest-area loop is the outline; loops strictly inside it are cut-outs (largest first; one
  inside / touching / crossing an accepted cut-out, or outside the outline, is reported as `loop_ignored`).
  Everything is moved so the outline's bounding box starts at (0, 0); coordinates within 1e-9 of a 1e-6 mm grid are
  snapped. Each cut-out is classified as Circle, Rectangle (axis-parallel, optional equal corner radius) or Other.
* **Untrusted input**: text ≤ `maxBytes` (32 MiB), ≤ `maxEntities` (100 000) entities, ≤ `maxLoopVertices`
  (10 000) per loop, coordinates ≤ 1e8 mm, ≤ 1000 loops / 20 000 segments per panel; the endpoint search is
  grid-indexed and bounded per lookup (no quadratic blow-up). Malformed input — a group code that is not an
  integer, a number that does not parse or is not finite, a pair or section cut off, no `ENTITIES`, no `EOF` — is a
  `ParseError` naming the line: `DXF line 24: group code 40 needs a finite number, found '1O.5'.`
* **Not supported**: export (see `drawings` for DXF output), blocks, splines/ellipses (no approximation is made),
  hatches as cut-outs, paper space selection, text/dimensions, binary DXF, DXF with `$INSUNITS` other than the five
  listed units (use `options.units`).

### 5.4 DXF part outline export — export only (`dxf_export.hpp`)

`exportPartOutlinesDxf(model, options)` writes the parts of a resolved model for CNC, nesting and cutting software.

* **Dialect: ASCII DXF R12 (`$ACADVER AC1009`)**, chosen because it is the dialect every CAD/CAM program reads
  (AutoCAD, LibreCAD, QCAD, DraftSight, nesting software). Sections: HEADER (`$ACADVER`, `$INSUNITS 4` = mm — an
  R2000 variable that R12 readers ignore; coordinates are mm either way —, `$EXTMIN`/`$EXTMAX`), TABLES (`LTYPE`
  CONTINUOUS, one `LAYER` per used layer plus `0`), an empty BLOCKS section, ENTITIES, EOF. No handles, no blocks,
  no INSERTs, ASCII only (non-ASCII text as `\U+XXXX`), `\n` line ends, numbers in shortest round-trip fixed
  notation (re-reading gives the same doubles).
* **Entities**: closed contours are 2D `POLYLINE`/`VERTEX`/`SEQEND` with flag 70 = 1; arcs are exact vertex bulges
  (group 42, the geometry::Contour convention) — never tessellated. Holes are `CIRCLE`s. The part id is a `TEXT`
  (height `textHeight`, default 10 mm) above the part.
* **Frame**: each part in its part frame seen from face A (u = x, v = y). Single file (default): parts in model
  order in a row along +X, each shifted by `(cursor − minX, 0)` of its finished-outline bounding box (shift rounded
  to 1e-6 mm), the cursor advancing by the box length + `spacing` (default 50 mm). `oneFilePerPart`: one file per
  part, part frame unshifted, file name `<part id, chars outside [A-Za-z0-9._-] as _>.dxf` (made unique with -2...).
  `partId` restricts the export to one part.
* **Layers**: `OUTLINE` — raw outline `domain::cutContour` (before edge banding; arcs keep their centre) or, with
  `finishedOutline`, `domain::finishedContour`; `CUTOUTS` — `PartShape` cut-outs and through pockets (face A or B);
  `POCKETS` — blind pockets on face A; `DRILL_<d>` — holes on face A and through holes on face B, one layer per
  diameter (`5` → `DRILL_5`, `8.5` → `DRILL_8_5`); `GROOVES` — grooves on face A as their closed rectangular
  footprint (centre line ± width/2); `LABELS` — part id text.
* **Not exported** (Warning `formats.dxf_export.operations_skipped`, one per part with counts): blind holes,
  grooves and blind pockets on face B and all edge-face operations — they cannot be drawn seen from face A.
  Depths are not encoded (layer per diameter only). Degenerate operations (diameter ≤ 0, zero-length groove,
  pocket < 3 vertices) → Warning `formats.dxf_export.invalid_operation`.
* **Errors**: no part / unknown `partId` (NotFound); `spacing` < 0 or `textHeight` ≤ 0 (InvalidArgument); a part
  whose raw outline cannot be computed or whose outline is not a valid contour (InvalidArgument naming the part).
* Round trip: `importDxf` with `layer = "OUTLINE"` returns the exported outline bit-exactly (tested on the
  banded rounded panel: radii 49.6 / 50 mm exact).

---

## 6. Tests

`ctest` target `fos_formats_tests` (GoogleTest): NIST SHA-256 vectors and padding boundaries; container shape,
round trip and every load error; migration chains; unknown-key preservation; truncation at every byte offset and
3000 randomly damaged files (must fail cleanly); atomic save with failure injected at each stage, backup
rotation, autosave and recovery; lossless codec round trip of `domain::fixtures::basicCabinet()` and of a model
using every field; every validation message; library merge policies; OBJ volumes, winding and frames; CSV rules.
Each test uses its own temporary directory and removes it.


`limits_tests.cpp`: FIFO and `/dev/zero` refused without blocking, 300 MiB sparse file refused from its size,
configurable limit at ±1 byte, `backupCount` 101 / 1e6 / -1 refused, listing-based rotation (foreign `.bak007`,
`.bak4` beyond the count, gaps), `importLibraryText` equal to `importLibrary` and bounded.
`shape_codec_tests.cpp`: byte-identical round trips (in memory and through a container file) of the PHASE 12
example parts (rounded 600 × 400 panel with per-segment bands, arc-topped 800 × 700 side, 1200 × 600 worktop with
sink and hob hole); rectangular encoding compared with a SHA-256 golden recorded before shapes existed; every
shape validation error with its exact path; limits at and beyond the boundary. `obj_shape_tests.cpp`: hand-computed
volumes of the example parts within the tessellation bound, closed and consistently oriented meshes, cap
triangles tiling the region exactly once (centroids in the material), rectangular OBJ/MTL output equal to the
pre-shape golden hashes, invalid shapes failing the export.
