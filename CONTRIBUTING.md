# Contributing to FurnitureOS

Thank you for helping build a free furniture CAD/CAM platform.

## Ground rules

* **Honesty first.** Never present a feature as working unless it is implemented and tested. Heuristics are
  called heuristics; unverified hardware or machine data is flagged as unverified.
* **Correctness over convenience** for anything that ends up as a manufacturing dimension.
* Read [`ARCHITECTURE.md`](ARCHITECTURE.md) before writing code. The dependency direction, unit, coordinate
  and dimension conventions there are binding.
* All contributions are licensed under **AGPL-3.0-only** (see [`LICENSE`](LICENSE)). Do not contribute code
  you do not have the right to license, code copied from proprietary products, or parsers produced by
  reverse-engineering formats in violation of their terms.

## Building and testing

Prerequisites: a C++20 compiler (GCC 13+, Clang 16+ or MSVC 2022), CMake 3.24+, Ninja, Qt 6.4+
(Widgets, OpenGLWidgets, PrintSupport, Svg), nlohmann/json 3.11+, GoogleTest.

```bash
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

Headless core only (no Qt): add `-DFOS_BUILD_GUI=OFF`. A subset of modules:
`-DFOS_MODULES="core;geometry;domain;cutting"`.

## Workflow

1. Open an issue describing the problem or proposal (for larger changes, before coding).
2. Branch from the default branch; never push to it directly.
3. Keep changes focused. Each module owns its directory (`MODULE_OWNERSHIP.md`); cross-module changes need
   agreement on the interface first.
4. Add tests. Engineering tests must compare against independently derived expectations (write the
   arithmetic in a comment), not against whatever the code currently returns.
5. `cmake --build` must be warning-free; `ctest` must pass. Do not disable failing tests.
6. Significant technical decisions get an ADR in `docs/decisions/`.

## Code style

* C++20, `.clang-format` in the repository root. Namespace `fos::<module>`.
* Expected failures are returned as `core::Result<T>` or `core::Diagnostic`s with specific, actionable
  messages. Exceptions are for programming errors only.
* No UI code in engineering modules; no engineering calculations in UI code.
* User-facing strings in the desktop app go through `tr()`; Ukrainian and English are maintained together.

## Reporting bugs

Include: version/commit, operating system, steps to reproduce, the project file if possible, expected and
actual result. For wrong dimensions or manufacturing output, include the numbers.

Security issues: see [`SECURITY.md`](SECURITY.md).
