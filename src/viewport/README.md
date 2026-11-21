# viewport — 3D view of a domain::Model (Qt 6 + OpenGL 3.3 core)

Module `fos::viewport` (deps: `fos::geometry`, `fos::domain`, Qt6 Widgets/OpenGL/OpenGLWidgets).
It only **displays** a resolved `domain::Model`; it never modifies it and never defines
authoritative geometry.

| Header | Content |
|---|---|
| `viewport_widget.hpp` | `ViewportWidget` (QOpenGLWidget) — integration contract; additions: `camera()`, `setCamera()` |
| `camera.hpp` | Qt-free orbit camera: presets, perspective/orthographic, matrices, pixel ⇄ ray |
| `scene.hpp` | Qt-free display scene: flattened parts, hidden/isolated/selected state, CPU picking, vertex arrays |

## Controls

| Input | Action |
|---|---|
| Left drag | Orbit about the camera target (0.4°/px; pitch limited to ±90°) |
| Middle drag, or Shift + left drag | Pan (the point under the cursor follows the cursor) |
| Wheel | Zoom to the cursor (factor 0.85 per notch) |
| Left click (moved < 4 px) | `partClicked(id, additive)`; empty id on empty space; additive = Ctrl or Shift |
| Double click on a part | `partDoubleClicked(id)` (no extra `partClicked` for the second release) |

Presets (`setViewPreset`, each followed by `fitAll()`): Front = observer at −Y looking along +Y
(the cabinet front, y = 0, faces you), Back, Left (observer at −X), Right, Top (front at the bottom
of the picture), Bottom, Isometric (observer at front-right-above, direction (−1, 1, −1)/√3). Z is up.
Perspective uses a 30° vertical field of view; orthographic uses the same half height at the target
plane (`distance · tan 15°`), so switching projection keeps the framing.

## Rendering approach

* One static VBO/IBO for **all** parts (24 vertices / 36 indices per part from
  `geometry::makeBox`, transformed by `Assembly::placement * Part::placement`), one line VBO with the
  12 box edges per part (`geometry::makeBoxEdges`). Each vertex carries its part index.
* Per-part colour (`Material::color`, grey 160/160/160 when the material is missing) and flags
  (visible, selected) live in a small RGBA8 texture read with `texelFetch` in the vertex shader.
  Selection / hiding / isolation only re-upload this texture (4 bytes per part); geometry is
  uploaded only when `setModel()` is called. Hidden parts are moved outside the clip volume in the
  vertex shader.
* Lighting: ambient 0.40 + diffuse 0.60 of one view-space directional light. Selected parts use the
  highlight colour (255, 140, 0) with a weak lighting term (0.85..1.0) so they stay unmistakable;
  their edges are drawn in a darker orange.
* `ShadedWithEdges` (default): faces with `glPolygonOffset(1, 1)` so the edge lines never z-fight;
  `Shaded`: faces only; `Wireframe`: edges only. Back-face culling on (meshes are outward CCW).
* Floor grid at z = 0 (100 mm minor, 1 m major lines, covering the parts + ≥ 0.5 m), axis triad in the
  bottom-left corner (X red, Y green, Z blue).
* Format request: OpenGL 3.3 core, 24-bit depth, 4× MSAA. When the context is older than 3.3, GLES,
  or a shader fails, a warning is logged (`core::log`, category `viewport`) and the view shows only
  the background; picking, camera and signals keep working.

## Contoured parts (ADR-0008)

Parts with a non-rectangular outline or cut-outs are drawn as the extrusion of their finished outline
(`shape_mesh.hpp`, `makeShapedPartGeometry`): contours tessellated with chord tolerance
`clamp(max(L, W) / 3000, 0.01, 0.2)` mm (`shapeChordTolerance`), caps triangulated with holes by bridge merging +
ear clipping (`geometry::triangulateWithHoles`, shared with the OBJ export), flat-shaded
side walls, watertight. Edge lines follow the outline and cut-outs at both faces plus vertical edges only at real
corners (tangent turn > 1°). Picking uses the part's mesh in the part frame, so a click into a sink cut-out or past
a rounded corner misses the part. A contour that cannot be triangulated falls back to the box
(`Scene::diagnostics()`, `viewport.shape.fallback_box`). Rectangular parts are unchanged (golden test).

## Picking

`partAt()` casts a CPU ray (`Camera::rayThroughPixel` at the pixel centre) against the oriented part
boxes with `geometry::intersectRayOrientedBox`; nearest hit wins, lowest part index on ties. It uses
the same camera math as the GL matrices (tested to agree within 0.01 px) and needs no GL context.
Hidden, non-isolated and zero/non-finite-size parts are not pickable.

## Performance (measured, Mesa llvmpipe, OpenGL 4.5, 4 shared CPU cores, under xvfb)

`ViewportGl.IsometricScreenshotAndFrameTime`, 334 basic cabinets = 2 004 parts, 1280×800:

* `setModel` + first frame (CPU mesh build + GPU upload + render + readback): **≈ 47 ms**
* orbiting frame including `grabFramebuffer()` readback of 1280×800: **≈ 26 ms** (≈ 38 fps upper bound
  on a software rasteriser; hardware GPUs are far faster).

## Measurement tool (phase 15)

`setMeasureMode(true)` (app action "Measure", key M). Qt-free math in `fos/viewport/snap.hpp`:

* **Snap candidates** (`partSnapPoints`): box parts give 8 corners, 12 edge midpoints, 6 face centres;
  contoured parts give the outline corners on faces A and B (ends of the vertical feature edges, i.e. where
  the tangent turns by > 1°) and the midpoints of those vertical edges. Points on the removed side of a
  section plane are dropped.
* **`snapAt`**: nearest candidate of any visible part within 12 logical px of the cursor whose occlusion ray
  (from the point, 0.05 mm off, toward the viewer) hits no part; ties by kind (corner, outline vertex, edge
  midpoint, face centre), then part/candidate order. Otherwise the free surface/cap point under the cursor
  (`Scene::pickHit`), else nothing.
* Two left clicks give a `Measurement` (distance, signed ΔX/ΔY/ΔZ = b − a); a third click starts a new one;
  dragging still orbits/pans; clicks emit no `partClicked`. Esc clears the points, a second Esc leaves the
  mode (`measureModeChanged(false)`). Overlay (QPainter after the GL frame): line (dashed rubber band while
  picking the second point), point dots, snap marker shaped by kind, distance label at the midpoint.
  `measurementChanged()` lets the app show the numbers in its status bar. Never touches the model.

## Section view (phase 15)

`setSectionPlane(SectionPlane{axis, position mm, flip})` / `std::nullopt` (`fos/viewport/section.hpp`).
`flip = false` keeps `coordinate <= position`, `true` keeps `>=`.

* Faces and edges are clipped in the vertex shaders with `gl_ClipDistance[0]` (the grid is not clipped).
* **Caps**: in the shaded modes the renderer toggles stencil bit 0 for every clipped surface fragment
  (no depth test, both facings), then draws one quad on the plane (covering all parts + 10 mm) in the flat
  cap colour (120, 84, 60) where the parity is odd, i.e. where the plane passes through material. One cap
  colour for all parts; parity assumes parts do not overlap and that the near plane does not cut a part.
  When the framebuffer has no stencil bits, no caps are drawn and back-face culling is turned off so cut
  parts are shown open (documented fallback).
* **Picking/snapping**: parts whose world box is entirely on the removed side are never hit; cut box parts
  are hit at the later of box entry and plane crossing (surface or cap); cut contoured parts are hit at the
  cap when the first mesh hit beyond the plane is a back face, else at that hit.

## Tests

* `fos_viewport_tests` (offscreen Qt platform, no GL): camera presets/projection/matrices/fit/zoom/pan,
  scene flattening and visibility, picking on `fixtures::basicCabinet()`, click vs drag signals;
  `snap_tests.cpp`: measurement math, snap candidates, radius/occlusion/cap snapping, section classification;
  picking with a section plane (removed shelf not picked, cut shelf picked at its cap) and a two-click
  measurement with Esc handling.
* `viewport_gl` (CTest label `gl`, run through `xvfb-run -a` when available): renders the fixture and
  checks pixels (part vs background, highlight colour, hidden part). Each test `GTEST_SKIP`s with the
  reason when no windowing platform / OpenGL 3.3 core context is available. Set
  `FOS_VIEWPORT_SCREENSHOT=/path.png` to save the 1280×800 isometric screenshot.

## Limitations

* Rectangular parts are drawn as finished-size boxes, contoured parts as flat-shaded extrusions (arcs look
  faceted within the chord tolerance): no operations (holes, grooves), edge-band colours, textures or hardware.
* No hidden-line removal in `Wireframe` (all edges are shown); no transparency, shadows or labels on the
  axis triad.
* Line width is 1 device pixel (wide lines are not available in core profile).
* No GPU-side frustum culling or LOD; all parts are always submitted (one draw call each for faces and
  edges).
* Section caps have one flat colour (not per material) and no hatch; the cap of overlapping parts has
  wrong parity. The GL cap path is not covered by an automated pixel test.
* Measurement snapping has no edge-nearest-point or arc-centre snaps, and contoured parts offer no face
  centres. The measurement is not stored in the project.
* `moc` of Qt 6.4 cannot parse libstdc++'s C++20 concepts, so the target sets
  `AUTOMOC_COMPILER_PREDEFINES OFF` (applies only to moc, not to compilation).
