# Project Status

_Last updated: 2026-11-12._ This file is the authoritative statement of what exists. Update it only with
facts that were verified by building and running tests.

## Overall

Early development. **Current phase: 2–3** (foundation and geometry/parametric done; furniture CAD in
progress). Engineering libraries for phases 5–6 were developed in parallel and are tested in isolation.
There is no desktop application, no native project file and no release.

## Build and test status (Linux, GCC 13.3, Ubuntu 24.04)

Verified together in one build on 2026-11-12:
`core, geometry, parametric, domain, machining, hardware, production, cutting, formats` — all tests pass
(one test skipped when run as root: read-only-folder save test).

Not verified: Clang, MSVC/Windows, macOS, the CI workflow itself (never run on GitHub yet), Qt modules.

## Task registry

| ID | Module / task | Owner role | Status | Verification | Review |
|---|---|---|---|---|---|
| T-001 | Build system, core, geometry base, domain model | Chief Architect | Done | 19 tests | Self |
| T-002 | ADRs, licence inventory, research surveys | Research + Licensing | Done | n/a (several facts marked unverified) | Pending |
| T-003 | Parametric engine | Parametric Engineer | Done | 56 tests incl. 10k-parameter graph | Pending independent review |
| T-004 | Geometry extensions | Geometry Engineer | Done | 72 tests | Pending |
| T-005 | Machining model | Machining Engineer | Done | 33 tests | Pending |
| T-006 | Hardware catalogue + placement rules | Hardware Engineer | Done (generic, unverified data) | 29 tests | Pending |
| T-007 | Production documents | Production Docs Engineer | Done | 44 tests | Pending |
| T-008 | Cutting optimiser | Cutting Engineer | Done (heuristic) | 55 tests, independent layout validator | Pending |
| T-009 | Formats: container, codecs, atomic save, libraries | Persistence Engineer | Done | 99 tests (1 skipped as root) | Pending |
| T-010 | Drawings | Drawing Studio Engineer | In progress | — | — |
| T-011 | Furniture definitions | Furniture CAD Engineer | In progress | — | — |
| T-012 | Validation | Validation Engineer | Not started | — | — |
| T-013 | Nesting | Nesting Engineer | Not started | — | — |
| T-014 | CAM foundation | CAM Engineer | Not started | — | — |
| T-015 | Plugins (declarative) | Plugin Engineer | Not started | — | — |
| T-016 | Project document, `.fosp`, commands/undo | App Architect | Not started (blocked by T-011) | — | — |
| T-017 | CLI + end-to-end scenarios A–E | Integration / QA | Not started (blocked by T-016) | — | — |
| T-018 | PDF export and exact-scale printing (Qt) | Printing Engineer | Not started (blocked by T-010) | — | — |
| T-019 | Desktop application (Qt) | Desktop UI Engineers | Not started (blocked by T-016) | Technical proof only: Qt OpenGL widget and PDF writer run in the dev environment | — |
| T-020 | Website (separate repo) | Website Engineers | Done (v1) | lint, type check, 11 tests, build | Statuses need refresh |
| T-021 | Website backend (separate repo) | Backend Engineers | Done (v1) | 167 tests | Docker image never built |
| T-022 | Independent architecture / code / security review | Reviewers | Not started | — | — |

## Known bugs and technical debt

### Independent review 2026-11-12 (calculation-critical modules) — fixes in progress
Verified correct by the reviewer (probes): single cut-size formula used everywhere; machining face frames;
parametric incremental recalculation (4,719 random edits vs. full rebuild, 0 mismatches); cutting validator
independent of the packer; byte-identical model round trip; a 1203.5 mm / 16 mm / OverSides / grooved-back /
3-partition cabinet checked by hand.

Confirmed defects being fixed:
* HIGH: intermediate hinges were not moved off adjustable shelves at default shelf clearance (wardrobe template:
  plate holes inside a shelf band).
* Fixed shelves on both sides of a partition get coincident confirmat through-holes (generation reports collisions).
* Pinned derived parameters survive construction changes silently (back could end up inside a side).
* Drawer fronts use the opposite face-A convention from doors.
* Back grooves run through banded/visible ends; shelf-pin depth reduced on outer sides too.
* Drawing text overrides and user dimensions do not follow model changes (stale numbers possible).
* Cutting adapter does not report a missing edge band; grain/rotation rule duplicated; cutting optimiser
  target links domain (layering).
* Five furniture test files were empty (fronts, interior, edit API, templates).

* `core::Error` has no stable machine code / source position field; `core::Diagnostic` has a single entity id.
* Domain types lack `operator==` and (for Operation, HardwareInstance, Assembly, Model) a `custom` map, so
  unknown JSON keys on those types are rejected rather than preserved.
* Geometry: no curves, no booleans, holes only partly supported; brute-force complexity.
* Cutting: adjacent non-sibling offcuts are not merged; no stack cutting; cost is only a tie-breaker.
* Default CMake configuration has no build type (unoptimised); packaging presets missing.
* ARCHITECTURE.md §3 lists `cutting` as core-only; the model adapter makes it depend on geometry and domain.
* Earlier commits on the development branch labelled "WIP" contain intermediate, non-integrated states.

## Decisions requiring owner input

1. **Default branch.** The repositories were empty; the development branch is currently the only branch.
   Create `main` (and branch protection) when you are ready to accept the work.
2. **Open CASCADE + AGPL-3.0-only** compatibility needs legal review before any OCCT backend is shipped.
3. **Website**: production domain, licence of the website source, review of Ukrainian text by a native speaker.
4. **Backend**: hosting target, allowed CORS origins, release asset naming and checksum file convention.
5. **LICENSE text** was taken from the SPDX licence list (gnu.org unreachable from the build environment);
   compare with the canonical gnu.org text.

## Next priorities

See [`ROADMAP.md`](ROADMAP.md) "Near-term priorities".
