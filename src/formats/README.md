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
| 1 | File missing, a directory, unreadable | `IoError` | path |
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
* `backupCount` (default 3): `0` creates no backups and leaves existing `.bak` files alone. `.bak1` is always the
  most recent previous version. Lowering the count later does not delete higher-numbered old backups.
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
| `custom` | object string→string | | `{}` | |

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

---

## 5. Exchange formats

### 5.1 Wavefront OBJ (+ MTL) — export only

`exportObj(model)` → OBJ text, MTL text, warnings. `writeObjFiles(path, model)` writes `<name>.obj` and
`<name>.mtl` (whitespace in the MTL file name replaced by `_`).

| | |
|---|---|
| Exported | Every part of every assembly as a box of its **finished** size `length × width × thickness`, positioned in the **project frame** (`assembly.placement * part.placement`). One `o` and one `g` per part (named after the part id), 8 `v`, 6 `vn`, 6 quad faces `f v//vn` with outward counter-clockwise winding. |
| Units | Millimetres, stated in the file header comment. No scaling. Coordinates are written with full double precision. |
| Axes | FurnitureOS project frame: right-handed, **Z up**. No conversion to the Y-up convention some viewers assume. |
| Materials | `usemtl <material id>` per part; the MTL file has one `newmtl` per used material with the display colour as `Kd` (4 decimals), `d 1`, `illum 1`. A part with no or an unknown material gets a neutral grey material and a `formats.obj.material_missing` warning. |
| Names | Whitespace, control characters and `#` in ids are replaced by `_`; clashes get `_2`, `_3`, … |
| Not exported | Machining operations (holes, grooves, pockets, cut-outs), edge bands, hardware, textures / UVs, grain direction, assembly hierarchy, costs, custom properties. |
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

---

## 6. Tests

`ctest` target `fos_formats_tests` (GoogleTest): NIST SHA-256 vectors and padding boundaries; container shape,
round trip and every load error; migration chains; unknown-key preservation; truncation at every byte offset and
3000 randomly damaged files (must fail cleanly); atomic save with failure injected at each stage, backup
rotation, autosave and recovery; lossless codec round trip of `domain::fixtures::basicCabinet()` and of a model
using every field; every validation message; library merge policies; OBJ volumes, winding and frames; CSV rules.
Each test uses its own temporary directory and removes it.
