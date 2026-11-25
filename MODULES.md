# Module Map

The code base is split into modules under `src/`, each built as a library `fos::<name>`. A module may only
depend on the modules listed for it (enforced by CMake). Changes to a module's public interface should be
agreed before code that depends on them is written.

| Path | Purpose | Depends on |
|---|---|---|
| `src/core/` | Result/error types, diagnostics, units, ids | — |
| `src/geometry/` | Vectors, transforms, bounding boxes, polygons, contours (lines and arcs), offsets, triangulation, meshes, ray picking | core |
| `src/parametric/` | Parameter graph, expressions, constraints | core |
| `src/domain/` | Parts, materials, edge bands, hardware definitions, assemblies, the generated model | core, geometry |
| `src/machining/` | Drilling and machining operations on parts | core, geometry, domain |
| `src/hardware/` | Generic fitting catalogue and placement rules (hinges, slides, connectors, tracks, hangers, rails) | + machining |
| `src/furniture/` | Cabinet definitions and the generator (sections, fronts, drawers, corners, appliances, sliding doors) | + parametric, hardware |
| `src/validation/` | Model checks: interference, machining conflicts, construction rules | core, geometry, domain, machining |
| `src/linear/` | 1D cutting optimiser and validator | core |
| `src/production/` | Part lists, cut lists, labels, edge banding, hardware reports, quotes, packing | core, geometry, domain |
| `src/cutting/` | Guillotine sheet cutting optimiser and validator | core (+ domain adapter) |
| `src/nesting/` | Nesting of shaped parts | core, geometry |
| `src/drawings/` | Drawing views, dimensions, layouts, SVG/DXF output | core, geometry, domain |
| `src/printing/` (Qt) | PDF and printer output at exact scale | drawings, Qt |
| `src/cam/` | Toolpaths, simulation, postprocessors | core, geometry, domain, machining |
| `src/formats/` | File containers and codecs (libraries, models, components, tool tables), exports | core, geometry, domain |
| `src/plugins/` | Data plugins (libraries, templates) | core, domain, formats |
| `src/project/` | Project file, documents with undo/redo, commands, outputs, batches, worktops, plinths, merge | all engineering modules |
| `src/cli/` | The `fos` command-line tool | project |
| `src/viewport/` (Qt) | OpenGL 3D view | geometry, domain, Qt |
| `src/app/` (Qt) | Desktop application | everything |
| `tests/` | Cross-module scenario tests | everything |
| `docs/decisions/` | Architecture decision records | — |
| `docs/research/` | Background research on algorithms, formats and CAM | — |
| `docs/user-guide/`, `docs/development/`, `docs/api/` | User and developer documentation | — |

The task registry (what is done and how it is tested) is in [`PROJECT_STATUS.md`](PROJECT_STATUS.md).
