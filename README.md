# FurnitureOS

Free, open-source furniture CAD/CAM: parametric cabinet design, technical drawings, production documents,
sheet cutting optimisation and (later) machine-specific manufacturing output — in one offline desktop
application, with no account and no paid features.

> **Status: early development — no release yet.** The engineering libraries, a command-line tool and a
> first desktop application exist and are covered by ~800 automated tests on Linux. Nothing has been used in
> real production yet. [`PROJECT_STATUS.md`](PROJECT_STATUS.md) is the authoritative statement of what works;
> nothing here should be read as more finished than that file says.

## Why

Furniture designers today move a project through several unrelated tools (CAD, cutting optimiser, drilling
and CNC software) and re-enter or manually correct dimensions on the way. Changing one cabinet width means
recalculating shelves, fronts, edge banding, hardware positions and drawings by hand. FurnitureOS keeps one
parametric model and derives everything else from it. It must work equally for a workshop with a panel saw
and a drill, and for a factory with CNC machines.

## Feature status

**Implemented** (works, tested) · **Partial** (works for the documented scope, gaps listed) ·
**Experimental** (works, but not validated for production use) · **Planned** · **Not started**.

| Area | Status | Scope and limits |
|---|---|---|
| Parametric engine (expressions, dependency graph, incremental recalculation, constraints, undo snapshots) | Implemented | `src/parametric` |
| Parametric cabinets (carcass variants, grooved/overlay/inset backs, section tree, shelves, partitions, doors, drawer fronts, 6 templates) | Partial | No drawer boxes; no corner or angled units |
| Materials, edge banding, finished vs cut size | Implemented | Libraries in `.foslib`; generic placeholder data only |
| Hardware placement (confirmat/dowel joints, shelf pins, hinges, handles) | Partial | Generic fittings, **unverified dimensions** |
| Drilling / machining model and validation | Implemented | Hole–hole collisions; grooves/pockets bounds only |
| Model validation (interference with groove engagement, mating holes, completeness) | Implemented | Parts treated as solid boxes |
| Production documents (part/cut lists, materials, edge banding, hardware, labels, cost; CSV/HTML/SVG) | Implemented | |
| Sheet cutting optimisation (guillotine, kerf, trim, grain, remnants, independent validator) | Implemented | Heuristic — not optimal |
| True-shape nesting (non-rectangular parts) | Partial | Heuristic; no part-in-hole; projects do not create shaped parts yet |
| Drawings (views, hidden lines, sections, dimensions, editable annotations, sheets; SVG/DXF) | Partial | Exact hidden lines only for axis-aligned panels |
| PDF export and exact-scale printing with calibration mark | Experimental | Not yet verified on physical printers |
| Native project file `.fosp` (versioned, checksummed, atomic save, backups, autosave, undo/redo) | Implemented | No migrations needed yet (format v1) |
| 3D viewport (orbit/pan/zoom, presets, picking, selection) | Experimental | Box display only (no holes/edge colours) |
| Desktop application (Qt 6; English and Ukrainian) | Experimental | Parameters, construction, interior sections, fronts, hardware, part overrides, drawing annotations, cutting settings |
| Command-line tool `fos` | Implemented | `src/cli/README.md` |
| CAM: toolpaths, simulator, generic G-code | Experimental | **No machine is verified.** Output must be proven by a qualified operator |
| Plugins | Partial | Declarative data plugins only (materials, edge bands, hardware); no code plugins |
| Import from other furniture CAD products | Not started | Research only (`docs/research/`) |
| Windows / macOS builds, installers, releases | Not started | Only Linux (GCC 13, Clang 18) is built and tested |

## Build and test

Prerequisites: C++20 compiler (GCC 13+ / Clang 16+), CMake 3.24+, Ninja, nlohmann/json 3.11+, GoogleTest,
and for the desktop application Qt 6.4+ (Widgets, OpenGLWidgets, PrintSupport, Svg, LinguistTools).

On Ubuntu 24.04:

```bash
sudo apt install g++ cmake ninja-build nlohmann-json3-dev libgtest-dev libgmock-dev qt6-base-dev libqt6opengl6-dev libqt6svg6-dev qt6-tools-dev qt6-l10n-tools libgl1-mesa-dev
```

Build everything and run the tests (GUI tests need a display; use `xvfb-run -a` on a headless machine):

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Start the desktop application with `./build/src/app/furnitureos`, or use the command-line tool, e.g.
`./build/src/cli/fos new kitchen.fosp --template base --width 800`. Headless build without Qt:
add `-DFOS_BUILD_GUI=OFF`. See [`docs/user-guide/getting-started.md`](docs/user-guide/getting-started.md).

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
