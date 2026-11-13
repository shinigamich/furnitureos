# Project Status

_Last updated: 2026-11-13._ This file is the authoritative statement of what exists. It is updated only
with facts verified by building and running tests.

## Overall

Early development, **no release**. The engineering core (phases 1–8 of [`ROADMAP.md`](ROADMAP.md)) exists as
tested libraries; a command-line tool and a first desktop application sit on top. Nothing has been used in
real production, no hardware data is verified against a manufacturer, and no CNC machine is supported.

Current phase: **3–4 (furniture CAD and Drawing Studio in the desktop UI)** plus phase 11-style hardening of
the libraries. Code size: ~61,000 lines of C++ in 20 modules (`src/`), plus `tests/` scenarios.

## Build and test status

Verified on 2026-11-13 (after the second review), Ubuntu 24.04, Qt 6.4.2, from clean build directories:

| Configuration | Result |
|---|---|
| Default (all 20 modules incl. Qt app), GCC 13.3, Release, `-DFOS_WERROR=ON`, tests under `xvfb-run` | builds; **852/852 tests pass** (1 skipped: read-only-folder save test cannot run as root) |
| Headless (`-DFOS_BUILD_GUI=OFF`), GCC 13.3, `-DFOS_WERROR=ON` | builds; 761/761 pass |
| Headless, Clang 18, Debug, `-DFOS_WERROR=ON` | builds; 730/730 pass (before the second-review fixes; re-run pending) |
| CLI end to end (`fos new` → `set width 900` → `report` → `validate` → `cut` → `drawing` → `cam`) | run by hand; results checked against hand-computed sizes |

Not verified: Windows, macOS, the GitHub Actions workflow itself (it has not run on GitHub yet), physical
printers, any real CNC machine or controller.

## Tests per module (GoogleTest cases)

core 10 · geometry 77 · parametric 56 · domain 8 · machining 33 · hardware 31 · furniture 52 · validation 43 ·
production 41 · cutting 50 + 7 (model adapter) · nesting 23 · drawings 51 · printing 30 · cam 40 · formats 99 ·
plugins 35 · project 33 · viewport 16 + 2 (OpenGL) · app 27 + GUI smoke · cli 26 · end-to-end scenarios A–E 16.

## Task registry

| ID | Module / task | Status | Verification | Review |
|---|---|---|---|---|
| T-001 | Build system, core, geometry base, domain model | Done | tests above | — |
| T-002 | ADRs, licence inventory, research surveys | Done | several facts marked unverified in the documents | — |
| T-003 | Parametric engine | Done | 56 tests; reviewer fuzzed 4,719 random edits vs full rebuild, 0 mismatches | Independent review: no defects |
| T-004 | Geometry kernel | Done | 77 tests | — |
| T-005 | Machining model | Done | 33 tests | Review: face frames verified |
| T-006 | Hardware catalogue + placement rules | Done (generic, unverified data) | 31 tests | — |
| T-007 | Production documents | Done | 41 tests | Review: grouping and areas verified |
| T-008 | Cutting optimiser (heuristic) + model adapter | Done | 57 tests; independent layout validator | Review: kerf/validator verified; layering fixed |
| T-009 | Formats: container, codecs, atomic save, libraries | Done | 99 tests | Review: byte-identical round trip verified |
| T-010 | Drawings | Done | 51 tests | Review finding fixed (stale overrides, model-anchored dimensions) |
| T-011 | Parametric furniture definitions | Done | 52 tests | 8 review findings fixed (see below) |
| T-012 | Model validation | Done | 43 tests; all 6 templates validate with 0 errors | — |
| T-013 | Nesting (heuristic) | Done | 23 tests; independent validator | — |
| T-014 | CAM foundation (unverified generic G-code) | Done | 40 tests; simulator catches corrupted programs | — |
| T-015 | Plugins (declarative data only) | Done | 35 tests | — |
| T-016 | Project document, `.fosp`, undo/redo | Done | 33 tests | — |
| T-017 | CLI + end-to-end scenarios A–E | Done | 26 + 16 tests | — |
| T-018 | PDF export and exact-scale printing | Done (experimental) | 30 tests; raster and PDF MediaBox measured | Not verified on a physical printer |
| T-019 | 3D viewport | Done (experimental) | 18 tests incl. OpenGL render test | — |
| T-020 | Desktop application | Done (experimental) | 27 tests + GUI smoke test; screenshots reviewed (en, uk) | — |
| T-021 | Website (separate repo) | Done (v1, statuses updated 2026-11-13) | lint, type check, 14 tests, build | Ukrainian text needs native review |
| T-022 | Website backend (separate repo) | Done (v1) | 167 tests | Docker image never built |
| T-023 | Independent review of calculation-critical modules | Done | findings below | — |
| T-024 | Desktop UI: interior/fronts/hardware/part-override/drawing-annotation/cutting editors | Done | 43 app-core tests + 5 GUI tests; en/uk screenshots reviewed | — |
| T-025 | Windows build bring-up | Not started | — | — |
| T-026 | Review of project, CAM, validation, nesting, plugins, CLI, printing | Done | all findings fixed with regression tests | — |

## Independent review 2026-11-12 (calculation-critical modules)

Verified correct by probes: single cut-size formula everywhere; machining face frames; parametric incremental
recalculation; cutting validator independence; byte-identical model round trip; a 1203.5 mm / 16 mm /
OverSides / grooved-back / 3-partition cabinet checked by hand.

Confirmed defects — all **fixed** with regression tests:
intermediate hinges not moved off adjustable shelves at default clearance · coincident confirmat
through-holes in shared partitions (now blind dowels) · pinned derived parameters surviving construction
changes silently (now a warning plus a generator interpenetration check) · drawer fronts with the opposite
face-A convention · back grooves notching banded ends · shelf-pin depth reduced on outer sides · drawing
text overrides going stale silently · missing edge bands not reported by the cutting adapter · duplicated
grain rule · cutting optimiser layering · five empty furniture test files.

## Independent review 2026-11-13 (newer modules)

Verified correct by probes: undo/redo over 300 random mixed commands restores the exact model; save is
byte-identical; malformed/truncated/newer files refused cleanly; 2,775 fuzzed CAM programs never go below the
stock bottom and output is always refused when simulation fails; validation detected all 1,207 injected 1 mm
overlaps across 72 template configurations; plugin path-escape attempts all refused; printing never scales
silently.

Confirmed defects — all **fixed** with regression tests (documented limits on file contents and model size,
O(1) generator lookups, capped backups, bounded regular-file reads, simulator safe-Z/clearance, coverage and
envelope checks, CLI exit 1 on incomplete output, single bounded plugin read, pin/joint collision rule):
* HIGH: a crafted `.fosp` (e.g. 10,000 shelves) hangs loading/generation — no size limits; quadratic generator lookup.
* HIGH: the CAM simulator trusted the machine's safe Z, so a hand-built program with a rapid into the stock passed
  and was post-processed (generated programs were not affected).
* A hostile backup count makes every save hang; `readFile` accepts devices/FIFOs and has no size cap.
* `fos cut` / `fos report` exit 0 with silently incomplete output when an item fails to generate.
* Simulator coverage gaps (sub-cell tools, last partial cell strip, negative stock z); plugin read TOCTOU;
  a dowel/shelf-pin hole collision in the wardrobe template at low heights.

## Known bugs and technical debt

* Documents may be saved while the generator reports errors (deliberate, to allow intermediate states);
  front ends must show the diagnostics (the app and `fos set` do).
* Interference depth is reported as minimum separation distance in validation but as overlap extent in the
  furniture self-check — both correct, worded differently.
* Part names from the generator are English only; the app cannot translate them yet.
* `formats::importLibrary` has no size limit of its own (plugins check size before reading).
* `core::Error` has no stable machine code field; `core::Diagnostic` carries a single entity id.
* Geometry: no curves or booleans; holes only partly supported; brute-force complexity.
* Several WIP snapshot commits exist on the development branch from parallel work; they contain intermediate states.

## Decisions requiring owner input

1. **Default branch.** The development branch is the only branch. Create `main` (and branch protection)
   when you are ready to accept the work; then CI will run on pull requests.
2. **Open CASCADE + AGPL-3.0-only** compatibility needs legal review before any OCCT backend ships.
3. **Website**: production domain, licence of the website source, native review of the Ukrainian text.
4. **Backend**: hosting target, allowed CORS origins, release asset naming and checksum file convention.
5. **LICENSE text** was taken from the SPDX licence list (gnu.org unreachable from the build environment);
   compare it with the canonical gnu.org text.
6. **Real hardware and machine data**: which manufacturers' fittings and which CNC controllers to verify
   first. This needs data sheets and, for CAM, access to the actual machine or its simulator.

## Next priorities

1. Re-run the Clang configuration; first CI run on GitHub once `main` exists.
2. Remove the remaining quadratic costs (pairwise hole validation, hardware placement scans) so model limits can be raised.
3. Windows build (T-025) and packaging; then a first tagged pre-release with checksums.
4. Verified data: one real hinge/connector system and one controller (e.g. LinuxCNC simulation) end to end.
