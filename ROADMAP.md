# Roadmap

Phases are completed only when the functionality works end to end and is verified by tests.
Current state per phase is tracked in [`PROJECT_STATUS.md`](PROJECT_STATUS.md).

| Phase | Goal | Deliverable |
|---|---|---|
| 0 | Repository and architecture audit | Architecture, ownership, ADRs, risk register |
| 1 | Engineering foundation | Build system, core, domain model, tests, file format foundation, CI, application shell |
| 2 | Geometry and parametric foundation | Geometry kernel, parametric dependency engine |
| 3 | Initial furniture CAD | Parametric cabinets, materials, edge banding, 3D viewport, property editing, undo/redo |
| 4 | Drawing Studio | Views, dimensions, editable layout, SVG/PDF, exact-scale printing with calibration |
| 5 | Production documentation | BOM, cut lists, material/edge/hardware reports, labels |
| 6 | Cutting optimisation | Kerf, grain, remnants, validated layouts, visualisation |
| 7 | Drilling, machining, nesting | Operation model, hardware-driven drilling, true-shape nesting |
| 8 | CAM foundation | Toolpath IR, machine profiles, postprocessor interface, simulation; first *verified* adapter only when it can be proven |
| 9 | Website and backend | Public site, downloads, release metadata |
| 10 | Plugins and interoperability | Declarative plugins, SDK docs, documented exchange formats |
| 11 | Production hardening | QA, performance, security and licence audits, packaging for Linux/Windows/macOS |

## Current state (2026-11-13)

| Phase | State |
|---|---|
| 0–2 | Done |
| 3 Furniture CAD | Engine done; desktop UI edits parameters, construction and materials — interior/front editors pending |
| 4 Drawing Studio | Generation, edits, SVG/DXF/PDF and print planning done; drawing-edit UI pending; physical print verification pending |
| 5 Production documentation | Done (libraries, CLI, app) |
| 6 Cutting optimisation | Done (heuristic, validated layouts) |
| 7 Drilling, machining, nesting | Done for the documented scope; nesting not yet fed by projects |
| 8 CAM foundation | Done; no machine-specific adapter verified |
| 9 Website and backend | v1 done |
| 10 Plugins and interoperability | Data plugins done; interoperability adapters not started |
| 11 Production hardening | Partly (independent review, warnings-as-errors builds, GCC + Clang); Windows/macOS and packaging not started |

## Near-term priorities

See [`PROJECT_STATUS.md`](PROJECT_STATUS.md) "Next priorities".
