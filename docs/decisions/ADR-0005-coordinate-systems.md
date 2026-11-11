# ADR-0005: Coordinate systems

* Status: Accepted
* Date: 2026-11-11

## Context

Furniture data passes through several frames: the room, an assembled cabinet, a single panel, a drawing sheet and a machine. Mirrored or ambiguous frames are a classic source of drilling errors (holes on the wrong face). All frames must be declared once and conversions confined to defined places.

## Decision

All 3D frames are **right-handed**.

| Frame | Definition |
|---|---|
| Project | Z up; X/Y on the floor. |
| Assembly | X = width (left to right seen from the front), Y = depth (front to back; the front is at y = 0), Z = height. Origin: left-front-bottom corner of the nominal envelope. `Assembly::placement` maps assembly to project. |
| Part | X = length, Y = width, Z = thickness; the part occupies `[0,L] x [0,W] x [0,T]`. `Part::placement` maps part to assembly and is always a proper rotation plus translation (no mirroring, determinant +1). |
| Drawing sheet | Millimetres on paper, origin bottom-left, Y up. SVG/PDF writers flip Y. |
| Machine | Defined per machine profile in `cam`; conversion from the part frame happens only there. |

### Faces and edges (authoritative text in `src/domain/include/fos/domain/operation.hpp`; do not re-interpret)

| Face | Plane | (u, v) | Tool enters along |
|---|---|---|---|
| A | z = T | u = x, v = y | -Z |
| B | z = 0 | u = x, v = y (not mirrored) | +Z |
| L1 | y = 0 | u = x, v = z | +Y |
| L2 | y = W | u = x, v = z | -Y |
| W1 | x = 0 | u = y, v = z | +X |
| W2 | x = L | u = y, v = z | -X |

* Depth is measured from the face surface into the material, in mm.
* Edge-face coordinates refer to the FINISHED part (ADR-0004).
* Pocket contours are counter-clockwise in face coordinates.
* Edge sides `L1/L2/W1/W2` use the same names as the faces and the same geometric positions (`part.hpp`).

## Alternatives considered

* **Y-up frame (graphics convention).** Common in rendering; rejected because furniture and CAD work are Z-up. The viewport converts for rendering only.
* **Mirrored face B (u = -x, as seen when flipping the panel).** Matches how an operator views the flipped panel and how some machine formats define the bottom face. Rejected in the model because a mirrored (u, v) makes the transform to the part frame improper for B only and invites errors; any mirroring is done by the exporter that needs it, documented per format.
* **Origin at panel centre.** Simplifies rotations but makes edge-referenced dimensions harder; rejected.
* **Left-handed frames** (some CAM/graphics APIs): rejected; conversion at the boundary.

## Consequences

* Every exporter and importer must state its own frame and convert; tests with an asymmetric test part (hole near one corner on each face) are mandatory for each format adapter.
* Rendering code may use another convention but must not define geometry.
* Operations on B are expressed in the same (x, y) as A, which is easy to model but may surprise users used to flipped-panel conventions; UI must label faces.

## Revisit when

* A target machine format requires a different native convention and conversions become error-prone (then add explicit typed frames in code).
* Non-planar or angled operations (e.g. angled drilling) are added: Face/tool direction semantics need extension.
