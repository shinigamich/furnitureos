# Third-party licences

Project licence: **AGPL-3.0-only** (SPDX identifier `AGPL-3.0-only`; text in the file `LICENSE`). This file is an engineering inventory, not legal advice. Items marked "not verified" were not checked against the upstream source in this session (only some GitHub pages were reachable; the Qt and Open CASCADE websites were not).

## 1. Dependencies in use

| Component | Licence | Linking / role | AGPL-3.0-only compatibility | Notes |
|---|---|---|---|---|
| Qt 6 (see section 2) | LGPL-3.0 (and GPL-2.0/GPL-3.0, or commercial) for the modules listed; from memory, not verified in this session | Dynamic linking, `app` and `printing` modules only | LGPL-3.0 libraries can be used by a GPL-3.0/AGPL-3.0 program (GPLv3/LGPLv3 permit combination; AGPLv3 section 13 covers AGPL-GPL combination). | Obligations in section 3. |
| GoogleTest | BSD-3-Clause (from memory, not verified here) | Tests only; not shipped | Compatible. | Keep out of release packages. |
| nlohmann/json | MIT (from memory, not verified here) | Header-only, in `formats`, `plugins`, `project` | Compatible. | Ship licence text with sources and binary packages (MIT requires the notice). |
| CMake | BSD-3-Clause (from memory) | Build tool | Not linked; not distributed in output. | |
| Ninja | Apache-2.0 (from memory) | Build tool | Not linked. | |

## 2. Qt 6 modules

| Module | Used by | Licence situation (from memory, not verified) |
|---|---|---|
| Qt Core | app, printing | LGPL-3.0 / GPL / commercial |
| Qt Gui | app, printing | LGPL-3.0 / GPL / commercial; bundles some third-party components with their own licences |
| Qt Widgets | app | LGPL-3.0 / GPL / commercial |
| Qt OpenGL | app | LGPL-3.0 / GPL / commercial |
| Qt OpenGLWidgets | app | LGPL-3.0 / GPL / commercial |
| Qt PrintSupport | printing | LGPL-3.0 / GPL / commercial |
| Qt Svg | app, printing | LGPL-3.0 / GPL / commercial |

These are the "Qt Essentials" style modules, which I believe are available under LGPL-3.0. **Verify each against the Qt documentation page "Licenses Used in Qt" (not reachable in this session) and the `LICENSES` directory / `REUSE.toml` of the Qt sources (the qtbase repository lists both, contents not read).**

**GPL-only Qt modules to avoid unless accepted** (from memory, not verified): add-on modules such as Qt Charts, Qt Data Visualization, Qt Virtual Keyboard, Qt Wayland Compositor and some Qt Quick extras (for example Qt Quick 3D Physics, Qt Lottie, Qt Quick Timeline) have been offered under GPL/commercial only, without an LGPL option. Policy: use only the modules in the table above; adding any other Qt module requires a licence check. GPL-3.0-only code is compatible with AGPL-3.0-only (via GPLv3 section 13) but would take the dependency out of the LGPL category and restrict future relicensing choices. GPL-2.0-only terms would not be compatible, so never rely on a GPL-2.0-only choice for a Qt module.

## 3. Obligations when distributing binaries (Qt under LGPL-3.0)

Summary of obligations (verify with the licence text and legal review):
* **Dynamic linking** against Qt shared libraries so users can replace them with their own modified versions. Do not link Qt statically unless you also provide object files or another means for relinking, which this project does not plan to do.
* **Include the LGPL-3.0 and GPL-3.0 texts** and Qt copyright notices; state that the application uses Qt and where the Qt source is available (give the exact Qt version and a written offer or link to the corresponding source).
* **Do not use technical measures that prevent replacement** of the Qt libraries (installation methods that lock libraries are problematic under LGPL-3.0, "Installation Information" requirement for user products).
* Qt libraries bundle third-party components (for example image libraries, ICU, HarfBuzz, FreeType, zlib, libpng) with their own licences; binary packages must carry their notices. Qt ships a list; check it for the exact Qt build and platform plugins used (e.g. the platform plugin and styles).
* FurnitureOS is AGPL: its own source must be offered to network users per AGPLv3 section 13 if it is modified and made accessible over a network; the desktop app has no network service, so this is mostly relevant for derived services.
* Packages must contain: `LICENSE` (AGPL text), this file, full licence texts of every shipped component, and information to obtain corresponding source.

## 4. Candidates (not used yet)

| Component | Licence | Linking | Compatibility notes | Open questions |
|---|---|---|---|---|
| Open CASCADE Technology | LGPL-2.1 with the OCCT exception (verified on the GitHub README; commercial licence also offered) | Dynamic linking recommended; optional backend only | Combining an LGPL-2.1-only library with an AGPL-3.0 program is the question to settle: LGPL-2.1 section 3 allows converting the library to GPL-2.0 terms, and GPL-2.0-only is not compatible with GPL-3/AGPL-3 code. The OCCT exception text may change this analysis. Not resolved here; legal review required before any OCCT backend is distributed. | Exact tag and licence of the version adopted; exception text; shipping size; whether a build option can keep it out of default packages. |
| Clipper2 | BSL-1.0 (verified) | Source/library | Compatible (permissive). | Active maintained repository (original shown as archived; maintenance moving to `PolyClipping/Clipper2`). Notice requirement applies to source copies only under BSL. |
| CGAL | LGPL-3.0-or-later for some packages, GPL-3.0-or-later for others (from memory, not verified) | Header-only mostly | Packages must be checked individually. | Which packages would be used. |
| Manifold | Apache-2.0 (from memory, not verified) | Library | Apache-2.0 is compatible with GPLv3/AGPLv3 (not with GPLv2-only). | Not currently needed. |
| libnest2d | LGPL-3.0 (verified) | Library | Compatible in principle; relinking obligations. | Not planned (see nesting survey). |
| SVGnest | MIT (verified) | Reference only | Compatible. | No code used. |
| Deepnest | Not established (no licence file seen on the repository opened) | Reference only | Do not copy code until clarified. | |
| LinuxCNC (rs274 interpreter) | GPL family (repository sidebar GPL-2.0, badges "Most LGPL-3 / Some GPL-2"; details not read) | Used as an external test tool only, not linked | Not distributed or linked, no effect. | If ever bundled, check GPL-2.0-only components. |
| Lua / WASM runtimes / Python | MIT / various (Wasmtime Apache-2.0 w/ LLVM exception, Wasmer MIT, WAMR Apache-2.0) / PSF (from memory) | Only if ADR-0007 phase 2 proceeds | Verify at adoption time. | |

## 5. Open questions

1. Confirm the Qt module licences against Qt's own licensing documentation and the exact Qt version to be shipped.
2. Decide how Qt is obtained for Windows/macOS packages (official installer vs. system package) and how corresponding source is offered.
3. Confirm the AGPL `LICENSE` text matches the gnu.org text byte for byte (see `KNOWN_LIMITATIONS.md`).
4. Legal review of OCCT (LGPL-2.1 + exception) combined with AGPL-3.0-only.
5. Legal review of plugin licensing implications (ADR-0007).
6. Add automated dependency licence scanning (SPDX/REUSE) to CI.
