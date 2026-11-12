# FurnitureOS

Free, open-source furniture CAD/CAM: parametric cabinet design, technical drawings, production documents,
sheet cutting optimisation and (later) machine-specific manufacturing output — in one offline desktop
application, with no account and no paid features.

> **Status: early development.** The engineering libraries below exist and are tested. There is **no
> desktop application and no release yet.** See [`PROJECT_STATUS.md`](PROJECT_STATUS.md) for the exact
> state; nothing in this repository should be read as more finished than that file says.

## Why

Furniture designers today move a project through several unrelated tools (CAD, cutting optimiser, drilling
and CNC software) and re-enter or manually correct dimensions on the way. Changing one cabinet width means
recalculating shelves, fronts, edge banding, hardware positions and drawings by hand. FurnitureOS keeps one
parametric model and derives everything else from it. It must work equally for a workshop with a panel saw
and a drill, and for a factory with CNC machines.

## Feature status

Statuses: **Implemented** (works, tested) · **Partial** · **Experimental** · **Planned** · **Not started**.

| Area | Status | Notes |
|---|---|---|
| Core (units, ids, errors, diagnostics) | Implemented | `src/core` |
| Geometry kernel (transforms, polygons, offset, triangulation, meshes, ray picking) | Implemented | Panel-furniture scope; no curves/booleans |
| Parametric engine (expressions, dependency graph, incremental recalculation, constraints, undo snapshots) | Implemented | `src/parametric` |
| Domain model (materials, edge bands, parts, operations, hardware) | Implemented | `src/domain` |
| Machining model (faces, validation, drilling patterns, ordering) | Implemented | Hole–hole collisions only |
| Hardware placement rules (confirmat/dowel joints, shelf pins, hinges, handles) | Partial | Generic, **unverified** fitting dimensions |
| Production documents (part list, cut list, materials, edge banding, hardware, labels, cost; CSV/HTML/SVG) | Implemented | `src/production` |
| Cutting optimisation (guillotine, kerf, trim, grain, remnants, validator) | Implemented | Heuristic — not optimal |
| File container, JSON codecs, atomic save + backups, libraries, OBJ export | Implemented | `src/formats` |
| Drawing generation (views, hidden lines, dimensions, sheets, SVG/DXF) | Partial | In progress |
| Parametric furniture definitions (cabinets, sections, fronts, templates) | Partial | In progress |
| Native project file (`.fosp`), undoable commands | Not started | |
| Model validation (interference, completeness) | Not started | |
| Nesting of non-rectangular parts | Not started | |
| CAM (toolpaths, machine profiles, postprocessors) | Not started | No machine is supported |
| Plugins (declarative data plugins) | Not started | |
| PDF export / exact-scale printing | Not started | |
| Desktop application (Qt 6) | Not started | |
| Import/export with other furniture CAD products | Not started | Research only (`docs/research/`) |
| Windows / macOS builds | Not started | Only Linux has been built and tested |

## Build and test

Prerequisites: C++20 compiler (GCC 13+ / Clang 16+), CMake 3.24+, Ninja, nlohmann/json 3.11+, GoogleTest.
Qt 6.4+ will be required for the desktop application once it exists.

```bash
cmake -S . -B build -G Ninja -DFOS_BUILD_GUI=OFF
cmake --build build
ctest --test-dir build --output-on-failure
```

On Ubuntu 24.04: `sudo apt install g++ cmake ninja-build nlohmann-json3-dev libgtest-dev libgmock-dev`.

## Documentation

* [`ARCHITECTURE.md`](ARCHITECTURE.md) — modules, dependency rules, units, coordinate systems, conventions.
* [`TECHNICAL_DECISIONS.md`](TECHNICAL_DECISIONS.md) and `docs/decisions/` — decision records.
* [`MODULE_OWNERSHIP.md`](MODULE_OWNERSHIP.md), [`PROJECT_STATUS.md`](PROJECT_STATUS.md), [`ROADMAP.md`](ROADMAP.md),
  [`KNOWN_LIMITATIONS.md`](KNOWN_LIMITATIONS.md).
* Module READMEs in `src/<module>/README.md`.

## Contributing

See [`CONTRIBUTING.md`](CONTRIBUTING.md) and [`CODE_OF_CONDUCT.md`](CODE_OF_CONDUCT.md). Security reports:
[`SECURITY.md`](SECURITY.md).

## License

FurnitureOS is licensed under the **GNU Affero General Public License v3.0 only** — see [`LICENSE`](LICENSE).
Third-party components: [`THIRD_PARTY_LICENSES.md`](THIRD_PARTY_LICENSES.md).
