# FurnitureOS Architecture

Status: living document. Binding for all contributors and automation agents.
Decisions with rationale live in `docs/decisions/` (ADRs) and are indexed in `TECHNICAL_DECISIONS.md`.

## 1. Ecosystem

| Repository | Visibility | Role |
|---|---|---|
| `furnitureos` | public, AGPL-3.0-only | Desktop application, engineering core, SDK. Authoritative for all furniture and manufacturing calculations. |
| `furnitureos-website` | private source, public site | Marketing, downloads, documentation portal. Works as a static site. |
| `furnitureos-website-backend` | private | Optional services (release metadata cache, plugin registry). Never a dependency of the desktop app. |

The desktop application never requires an account or a network connection. Cross-repository
contracts are specified in `docs/architecture/cross-repo-contracts.md`.

## 2. Technology (see ADR-0001, ADR-0002)

* C++20, CMake >= 3.24, Ninja. Compilers: GCC 13+, Clang 16+, MSVC 2022.
* Engineering core: standard library only (+ nlohmann/json inside `formats`, `plugins`, `project`).
  The core is headless and fully testable without a display.
* Desktop UI: Qt 6 Widgets + QOpenGLWidget (LGPL-3.0, dynamically linked). Only `printing`, `viewport` and `app` may use Qt.
* Tests: GoogleTest.
* Geometry: FurnitureOS' own kernel for panel furniture (boxes, 2D contours, prisms). Open CASCADE is a
  planned *optional* backend behind `geometry` for STEP exchange and free-form solids (ADR-0002); nothing depends on it.

## 3. Modules and dependency direction

Each module is a static library `fos::<name>` in `src/<name>/` with public headers in
`src/<name>/include/fos/<name>/`, sources in `src/<name>/src/`, tests in `src/<name>/tests/`,
namespace `fos::<name>`. A module may depend only on modules *above* it in this table, and only
on the ones listed for it.

| # | Module | Responsibility | May depend on |
|---|---|---|---|
| 1 | `core` | Result/Error, typed ids, units & tolerances, diagnostics, logging | — |
| 2 | `geometry` | Vectors, rigid transforms, AABB, 2D polygons, meshing, intersections | core |
| 3 | `parametric` | Expressions, parameters, dependency graph, incremental recalculation | core |
| 4 | `domain` | Plain data model: Material, EdgeBand, Part, Operation, Hardware, Assembly, Model; cut-size rule | core, geometry |
| 5 | `machining` | Operation geometry helpers, drilling patterns, operation validation, ordering | core, geometry, domain |
| 6 | `hardware` | Fitting catalogue, placement rules producing hardware instances + operations | …, machining |
| 7 | `furniture` | Parametric furniture definitions (cabinets, sections, fronts), construction rules, templates -> `domain::Assembly` | core, geometry, parametric, domain, machining, hardware |
| 8 | `validation` | Model-level checks (collisions, materials, edges, machining) -> Diagnostics | core, geometry, domain, machining |
| 9 | `production` | BOM, cut list, edge/hardware/drilling reports, labels, cost; CSV | core, geometry, domain |
| 10 | `cutting` | Rectangular guillotine cutting optimisation + layout validation (`fos::cutting`); adapter from `domain::Model` to cutting jobs in the separate target `fos::cutting_model` | core (`fos::cutting`); cutting + domain (`fos::cutting_model`) |
| 11 | `nesting` | True-shape nesting of polygons | core, geometry |
| 12 | `drawings` | Views, dimensions, annotations, sheets, SVG/DXF output | core, geometry, domain |
| 13 | `cam` | Machine profiles, toolpaths, postprocessor interface, CAM validation | core, geometry, domain, machining |
| 14 | `formats` | Versioned JSON container, migrations, atomic save, library import/export, OBJ/DXF/CSV exchange | core, geometry, domain |
| 15 | `plugins` | Plugin manifests, discovery, declarative extension loading | core, domain, formats |
| 16 | `project` | `Project` document (furniture definitions + libraries + drawing edits + settings), native `.fosp` (de)serialisation, undoable commands | all of the above |
| 17 | `cli` | Headless command-line front end (`fos`) | project and below |
| 18 | `printing` (Qt) | PDF export and exact-scale printing of drawing sheets | drawings, QtGui/QtPrintSupport |
| 19 | `viewport` (Qt) | 3D display and picking of a resolved model (display only, never authoritative geometry) | geometry, domain, Qt Widgets/OpenGL |
| 20 | `app` (Qt) | Desktop application: windows, viewport, inspectors, workspaces | everything |

Rules:
* UI code contains no manufacturing calculations. Rendering never defines authoritative geometry.
* `domain::Model` is **derived** data produced by `furniture`/`project`; downstream modules treat it as read-only input.
* Reports, drawings and cutting plans are always regenerated from the model — no stored copies that can go stale.
  Only *user edits* to derived artefacts (e.g. a moved dimension) are stored, keyed by stable ids.
* No global mutable state except the log sink. No threads until a module documents its ownership model.

## 4. Conventions that every module must follow

### 4.1 Units and precision (ADR-0003)
Millimetres (double) and degrees everywhere in the model. No rounding of intermediate values.
`core::kLengthEps` = 1e-6 mm for comparisons. Display/report rounding via `core::formatLength` (0.1 mm default).

### 4.2 Coordinate systems (ADR-0005) — all right-handed
* **Project frame**: Z up; X/Y on the floor.
* **Assembly frame**: X = width (left→right seen from the front), Y = depth (front→back; front at y = 0),
  Z = height. Origin: left-front-bottom corner of the nominal envelope.
* **Part frame**: X = length, Y = width, Z = thickness; the part occupies `[0,L]×[0,W]×[0,T]`.
  `Part::placement` maps the part frame to the assembly frame and is always a proper rotation + translation.
* **Faces / edges**: A (z=T), B (z=0), L1 (y=0), L2 (y=W), W1 (x=0), W2 (x=L). Face coordinates are defined in
  `fos/domain/operation.hpp` and must not be re-interpreted.
* **Drawing sheet frame**: millimetres on paper, origin bottom-left, Y up. Writers flip Y for SVG/PDF.
* **Machine frame**: defined per machine profile in `cam`; conversion from the part frame happens only there.

### 4.3 Dimension convention (ADR-0004)
`Part::length/width` are finished sizes including edge banding. Cut size is computed **only** by
`domain::cutSize()`. Grain, when present, is stated per part (`GrainDirection`) relative to the part frame.

### 4.4 Identity
Typed string ids (`core::TypedId`). User-created entities: UUID v4. Generated entities: deterministic
path ids `<owner>/<slot>` (e.g. `cab1/side.left`, `cab1/shelf.2`) stable across recalculation and reload.

### 4.5 Errors
Expected failures return `core::Result<T>` with a specific English message naming the entity and the numbers
involved. Validation findings use `core::Diagnostic` with stable codes `module.topic.problem`.
Exceptions only for programming errors. Never abort on bad user input or a bad file.

### 4.6 Text and localisation
The engineering core emits English messages and stable codes. All user-facing UI strings in `app` go through
Qt's `tr()`; translations (uk, en) live in `src/app/translations/`. Files are UTF-8.

### 4.7 Honesty rule
Nothing may be labelled as working unless it is implemented and covered by tests. Heuristics are called
heuristics. Unverified hardware and machine data are flagged as unverified in data and in the UI.

## 5. Data flow

```
Project (authoritative, saved)                 derived on demand
┌──────────────────────────────┐   resolve   ┌──────────────┐   ┌─ production (BOM, cut list, labels)
│ libraries (materials, edges, │ ──────────► │ domain::Model│ ─►├─ cutting / nesting
│ hardware) · furniture items  │  parametric │ (assemblies, │   ├─ drawings ─► printing (PDF/print)
│ (parameters, rules) · drawing│   engine    │ parts, ops)  │   ├─ validation
│ edits · settings             │             └──────────────┘   └─ cam (toolpaths → postprocessor)
└──────────────────────────────┘
```

## 6. Native project format (ADR-0006)
`.fosp`: UTF-8 JSON envelope `{ "format": "furnitureos-project", "formatVersion": N, "app": …, "checksum": …, "payload": {…} }`.
Sequential migrations N→N+1; newer-than-supported files are refused with a clear message (never partially loaded);
unknown keys are preserved on round-trip; saves are atomic (temp file + fsync + rename) with rotating backups.

## 7. Build layout
Root `CMakeLists.txt` adds every `src/<module>/CMakeLists.txt` that exists. Modules use
`fos_add_module()` / `fos_add_tests()` from `cmake/FosModule.cmake`. Developers can configure a subset with
`-DFOS_MODULES="core;geometry;domain;<mine>"` and headless builds with `-DFOS_BUILD_GUI=OFF`.
