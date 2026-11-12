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

## Near-term priorities

1. Finish drawings and parametric furniture definitions; integrate.
2. `project` module: `.fosp` save/load, undoable commands, resolve to the engineering model.
3. Headless CLI covering the full chain (create cabinet → change width → reports → cutting → drawings → save/reopen)
   with end-to-end scenario tests A–E.
4. Desktop application shell with 3D viewport and property editing.
5. Validation, nesting, CAM foundation, plugins; PDF/print module.
6. Windows build bring-up.
