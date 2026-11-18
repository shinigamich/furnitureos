# Roadmap

Phases are completed only when the functionality works end to end and is verified by tests.
Current state per phase and the task registry (T-numbers) are tracked in
[`PROJECT_STATUS.md`](PROJECT_STATUS.md).

## Product scope

FurnitureOS is aimed at becoming one integrated, free furniture engineering and manufacturing platform:
professional 2D/3D CAD, parametric and constraint-based design for all furniture types (cabinet,
residential, office, upholstered, custom), materials/hardware/connection systems, engineering rules and
validation, drawings and production documents, sheet and linear cutting, irregular nesting, machining and
CAM with machine-specific postprocessors and simulation, rendering, interoperability, and an SDK.
It deliberately excludes CRM/ERP, supplier portals and other business-management functions.

Phases 0–11 established the engineering foundation and the first end-to-end workflow for panel furniture
(MVP). Phases 12–22 extend that foundation to the full product scope. Each later phase builds on the
same authoritative model (ARCHITECTURE.md §3, §5): new capabilities are new modules or extensions, not
parallel models.

### Foundation phases (MVP)

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

## Expansion phases

| Phase | Goal | Depends on | Key deliverables |
|---|---|---|---|
| 12 | Geometry kernel II: curves and contours | 2 | 2D curves (lines, arcs, bulge polylines), exact arc offset, robust 2D Booleans, tessellation with tolerance; **contoured parts** (outline + cut-outs) through domain, furniture, drawings, viewport, nesting, CAM contour routing, validation, formats/project, app |
| 13 | Geometry kernel III: solids | 12 | B-rep or mesh-Boolean solids for non-panel parts (solid-wood profiles, legs, upholstery frames), STEP exchange via an optional OCCT backend after licence review (ADR-0002) |
| 14 | General parametric assemblies and constraints | 12 | Constraint solver (2D sketch constraints; 3D assembly mates), user-defined parametric components, component libraries, nested assemblies beyond cabinets |
| 15 | Direct modelling in the viewport | 12, 14 | Picking and editing of real engineering geometry (faces, edges, contours), snapping, measurement, section views, manipulators driving parameters through commands |
| 16 | Universal furniture domains | 13, 14 | Frame and solid-wood furniture, tables/chairs, office systems, wardrobes with sliding doors, upholstered furniture structures (frames, foam, fabric patterns via nesting) |
| 17 | Connection systems and engineering rules | 14 | Verified manufacturer hardware libraries (data sheets), rule engine for construction standards, collision/assembly-sequence checks |
| 18 | Linear cutting and advanced optimisation | 6 | 1D cutting (profiles, edging, worktops), multi-objective/batch optimisation, production batching, offcut/remnant management |
| 19 | CAM II: machines and postprocessors | 8, 12 | Machine definitions (routers, point-to-point, 5-axis later), contour/pocket toolpaths with arcs, tool libraries, **first verified postprocessor** (e.g. LinuxCNC in simulation), collision simulation |
| 20 | Rendering and visualisation | 15 | Materials/textures, realistic rendering, presentation output |
| 21 | Interoperability | 12, 13 | DXF/SVG import of contours, STEP/OBJ/STL exchange, documented exchange with cutting/CAM software; proprietary formats only where legally and technically documented |
| 22 | Code plugins and SDK | 14 | ADR-0007 phase 2: sandboxed or out-of-process code plugins with permissions; public C++/scripting API |

Revisit as the scope grows: ADR-0002 (own kernel vs OCCT for solids, phase 13), ADR-0006 (packaged
`.fosp` archive once textures/meshes are stored, phase 13/20), ADR-0007 (code plugins, phase 22),
ARCHITECTURE.md §4.3 (finished vs cut size for contoured and curved parts, phase 12).

## Current state (2026-11-16)

| Phase | State |
|---|---|
| 0–2 | Done |
| 3 Furniture CAD | Done for panel furniture (engine + desktop editors) |
| 4 Drawing Studio | Done; physical print verification pending |
| 5 Production documentation | Done |
| 6 Cutting optimisation | Done (heuristic, validated layouts) |
| 7 Drilling, machining, nesting | Done for the documented scope; nesting not yet fed by projects (→ phase 12) |
| 8 CAM foundation | Done; no machine-specific adapter verified (→ phase 19) |
| 9 Website and backend | v1 done |
| 10 Plugins and interoperability | Data plugins done; interoperability → phase 21; code plugins → phase 22 |
| 11 Production hardening | Linux done (GCC + Clang, warnings as errors); Windows done (MSVC, portable ZIP in CI, T-025); macOS, installers, signing not started |
| 12 Geometry kernel II | Done for lines and arcs (T-027–T-034); splines and nesting layouts in the desktop UI remain |
| 13–22 | Not started |

## Near-term priorities

See [`PROJECT_STATUS.md`](PROJECT_STATUS.md) "Next priorities".
