# Module Ownership and Task Registry

Each module directory has exactly one owning engineering role. Parallel work is isolated by directory:
an owner edits only its directories; interface changes to another module are requested from the
integration lead (Chief Architect), who also owns the root build files and top-level documents.

Development so far was carried out by specialised automation agents coordinated by a lead; the "role"
column records the responsibility so future contributors (human or automated) can pick it up.

## Ownership map

| Path | Owning role | Depends on |
|---|---|---|
| `CMakeLists.txt`, `cmake/`, top-level `*.md`, `.github/` | Chief Architect / Integration + DevOps | — |
| `src/core/` | Chief Architect | — |
| `src/geometry/` (base: vec, transform, aabb) | Chief Architect | core |
| `src/geometry/` (polygons, offset, triangulation, mesh, ray, projection) | Geometry Kernel Engineer | core |
| `src/parametric/` | Parametric Engine Engineer | core |
| `src/domain/` | Domain Model Architect | core, geometry |
| `src/machining/` | Drilling & Machining Engineer | core, geometry, domain |
| `src/hardware/` | Hardware & Fittings Engineer | + machining |
| `src/furniture/` | Furniture CAD Engineer | + parametric, hardware |
| `src/validation/` | Simulation & Validation Engineer | core, geometry, domain, machining |
| `src/production/` | Production Documentation Engineer | core, geometry, domain |
| `src/cutting/` | Cutting Optimisation Engineer | core (+ domain adapter) |
| `src/nesting/` | Nesting Engineer | core, geometry |
| `src/drawings/` | Drawing Studio + Layout Engineers | core, geometry, domain |
| `src/printing/` (Qt) | Printing Engineer | drawings, Qt |
| `src/cam/` | CAM & Postprocessor Engineer | core, geometry, domain, machining |
| `src/formats/` | Persistence & Interoperability Engineer | core, geometry, domain |
| `src/plugins/` | Plugin & SDK Engineer | core, domain, formats |
| `src/project/` | Desktop Application Architect | all engineering modules |
| `src/cli/` | Integration Engineer | project |
| `src/app/` (Qt) | Desktop UI/UX + Visualisation Engineers | everything |
| `tests/` (cross-module scenarios) | Testing Architect / QA | everything |
| `docs/decisions/`, `docs/research/`, `THIRD_PARTY_LICENSES.md` | Research + Licensing Engineers | — |
| `docs/user-guide/`, `docs/development/`, `docs/api/` | Documentation Engineer | — |

Independent review roles (Architecture Reviewer, Code Reviewer, QA, Security) own no code; their findings are
recorded in `PROJECT_STATUS.md` and fixed by the module owner.

## Task registry

The live task table (id, module, status, acceptance, review state) is maintained in
[`PROJECT_STATUS.md`](PROJECT_STATUS.md).
