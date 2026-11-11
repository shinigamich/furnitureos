# ADR-0001: Language, build system and UI stack

* Status: Accepted
* Date: 2026-11-11
* Deciders: chief architect (see `ARCHITECTURE.md` section 2)

## Context

FurnitureOS is an open-source (AGPL-3.0-only) desktop application for parametric furniture design, drawings, production documents, cutting optimisation and CAM. The engineering core must be:

* numerically careful (geometry, tolerances, cut sizes), deterministic and testable headless;
* portable to Windows, Linux and macOS as a native desktop application with no account or network dependency;
* able to host a 3D viewport and precise 2D drawings with exact-scale printing;
* maintainable by a small open-source team, with a licence-compatible dependency set.

## Decision

* **Language**: C++20. Supported compilers: GCC 13+, Clang 16+, MSVC 2022.
* **Build**: CMake >= 3.24 with Ninja as the default generator. Modules are static libraries `fos::<name>`.
* **Desktop UI**: Qt 6 Widgets (plus `QOpenGLWidget` for the viewport), used under LGPL-3.0 with **dynamic linking**. Only the `app` and `printing` modules may include Qt headers. The engineering core (`core` ... `project`, `cli`) is standard C++ and is built and tested without Qt (`-DFOS_BUILD_GUI=OFF`).
* **Tests**: GoogleTest (test-only dependency, not shipped).
* **JSON**: nlohmann/json, confined to `formats`, `plugins` and `project`.
* Localisation through Qt `tr()` in `app` only; the core emits English messages and stable codes.

## Alternatives considered

| Option | For | Against (for this project) |
|---|---|---|
| Rust (+ egui/Slint/Tauri) | Memory safety; good package manager | CAD/CAM ecosystem is thin (kernel bindings, DXF/STEP libraries, print/PDF with exact scale); desktop UI toolkits are less mature for dense inspector-style CAD UIs; Slint is GPL-3.0/commercial/royalty-free-with-conditions (licence terms not re-verified in this session); smaller pool of contributors who know both CAD and Rust. A later Open CASCADE backend (ADR-0002) would need C++ FFI anyway. |
| C#/.NET + Avalonia | Productive UI, garbage collection | Runtime must ship or be installed; interop with C++ geometry libraries needed for any serious kernel; GC pauses and layout of numeric arrays are less predictable for geometry-heavy loops; 3D viewport requires extra work. |
| Electron / web stack | Fast UI iteration, shared code with website | Heavy runtime; numeric core would be JavaScript/WASM; exact-scale printing and file-system safety (atomic saves) are harder; conflicts with "no network, no account, small native tool" goal. A WASM build of the core remains possible later because the core is Qt-free. |
| Python (PySide6/PyQt) | Very fast prototyping, large scientific ecosystem | Performance and packaging of a distributable desktop app (interpreter and wheels must be bundled per platform); type safety for a long-lived geometry core is weaker. Kept as a candidate for out-of-process plugins/scripting (ADR-0007). |
| Qt Quick/QML instead of Widgets | Modern look, animations | Dense engineering UIs (tables, trees, property editors, docking) are mature in Widgets; some Qt Quick add-ons are GPL-only (see `THIRD_PARTY_LICENSES.md`). |
| GTK / wxWidgets / Dear ImGui | wxWidgets has a permissive-ish licence; ImGui is MIT | Weaker printing/PDF, accessibility, i18n and 3D-widget story than Qt, or immediate-mode UI unsuitable for forms. |

## Consequences

Positive:
* One language for kernel, algorithms, CLI and UI; direct access to C++ geometry libraries (OCCT, Clipper2, CGAL candidates).
* Qt provides printing, PDF, SVG, i18n, accessibility and OpenGL integration on all three platforms.
* Headless core allows CI without a display and a CLI front end.

Negative / costs:
* C++ memory-safety risks; mitigated by warnings-as-errors, sanitizers in CI and `Result<T>`-based error handling (ARCHITECTURE.md 4.5). Not yet verified in CI in this session.
* Qt is a large dependency with LGPL-3.0 obligations (relinking, notices, no static linking without extra steps). See `THIRD_PARTY_LICENSES.md`.
* Dependency management across three platforms (vcpkg/Conan/system packages) is not yet decided and is a build-engineering task.
* Qt Commercial would be a separate licence; not used.

## Revisit when

* LGPL-3.0 obligations or Qt's open-source licensing change materially.
* A web or mobile front end becomes a requirement (then evaluate WASM build of the Qt-free core).
* A mature Rust CAD kernel with STEP support and a CAD-grade UI toolkit exists and contributors prefer it.
