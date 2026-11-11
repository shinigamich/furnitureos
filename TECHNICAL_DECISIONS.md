# Technical decisions index

Architecture Decision Records live in `docs/decisions/`. Format: Status, Context, Decision, Alternatives considered, Consequences, Revisit when.

| ADR | Title | Status | Decision in one line |
|---|---|---|---|
| [0001](docs/decisions/ADR-0001-language-build-and-ui-stack.md) | Language, build and UI stack | Accepted | C++20 + CMake/Ninja; Qt 6 Widgets (LGPL-3.0, dynamic) for the UI only; GoogleTest; nlohmann/json; headless Qt-free core. |
| [0002](docs/decisions/ADR-0002-geometry-kernel.md) | Geometry kernel | Accepted | Own lightweight kernel for panel furniture (boxes, contours, prisms, rule-based HLR); Open CASCADE as a planned optional backend for STEP/free-form; Clipper2 a candidate for 2D offsets. |
| [0003](docs/decisions/ADR-0003-units-and-precision.md) | Units and precision | Accepted | Millimetre doubles, degrees, eps 1e-6 mm, display rounding 0.1 mm, no intermediate rounding. |
| [0004](docs/decisions/ADR-0004-finished-vs-cut-dimensions.md) | Finished vs cut dimensions | Accepted | Part length/width are finished sizes; cut size = finished minus edge-band thickness, computed only by `domain::cutSize()`. |
| [0005](docs/decisions/ADR-0005-coordinate-systems.md) | Coordinate systems | Accepted | Right-handed frames (project, assembly, part, sheet, machine); faces A/B/L1/L2/W1/W2 with fixed (u,v) definitions. |
| [0006](docs/decisions/ADR-0006-native-project-format.md) | Native project format | Accepted | JSON envelope `.fosp` with formatVersion, checksum, sequential migrations, unknown-key preservation, atomic save with backups. |
| [0007](docs/decisions/ADR-0007-plugin-safety-model.md) | Plugin safety model | Proposed | Phase 1 declarative data plugins only; phase 2 sandboxed or out-of-process plugins with permission manifest; native plugins off by default. |

Related: `ARCHITECTURE.md`, `THIRD_PARTY_LICENSES.md`, `KNOWN_LIMITATIONS.md`, `docs/research/`.
