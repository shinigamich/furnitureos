# viewport — 3D view of a domain::Model (Qt 6 + OpenGL 3.3 core)

Module `fos::viewport` (deps: `fos::geometry`, `fos::domain`, Qt6 Widgets/OpenGL/OpenGLWidgets).
It only **displays** a resolved `domain::Model`; it never modifies it and never defines
authoritative geometry.

| Header | Content |
|---|---|
| `viewport_widget.hpp` | `ViewportWidget` (QOpenGLWidget) — integration contract; additions: `camera()`, `setCamera()` |
| `camera.hpp` | Qt-free orbit camera: presets, perspective/orthographic, matrices, pixel ⇄ ray |
| `scene.hpp` | Qt-free display scene: flattened parts, hidden/isolated/selected state, CPU picking, vertex arrays, Realistic-mode surface attributes |

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

## Realistic material display (phase 20)

`DisplayMode::Realistic` (app: View → Display mode → "Realistic materials", key 4; the default stays
`ShadedWithEdges`). Same single draw call: a second VBO with one `SurfaceVertex` per `MeshVertex`
(`scene.hpp`, 40 bytes: surface colour, `u`/`v` in mm, grain direction, `SurfaceKind`, texture index)
feeds attribute locations 3–7. `MeshVertex` and the golden geometry hash are unchanged.

* **Board faces** (A/B): material colour; `u` runs along the grain (`Part::grain`: AlongWidth → local y,
  otherwise local x), `v` across it; grain direction = that axis through `world`, 0 when the material has
  no `hasGrain`.
* **Edges**: banded edges (`Part::edges` by EdgeSide for boxes, `PartShape::outerEdges` per contour segment
  for contoured parts via `ShapedPartGeometry::wallSegment`) take `EdgeBand::color`; raw edges and cut-out
  walls take `rawEdgeColor(material) = 0.75·(0.7·(176,146,108) + 0.3·material)` with a particleboard
  speckle. Edge `u` runs along the edge, `v` = local z.
* **Procedural grain** (fragment shader, no textures): value-noise bands ~9 mm apart across the grain with
  low-frequency wobble, fine pores, slow tone drift, ±~5–10 % brightness; plus a faint anisotropic
  (Kajiya-Kay) sheen along the grain. Each part gets a seed offset so neighbours differ. **Anti-moiré**:
  each term fades out by `fwidth(uv)` before its period reaches ~2 px (bands 1–3 mm/px, pores
  0.12–0.45 mm/px, speckle 0.25–0.9 mm/px).
* **Image textures**: `Material::texture` naming a readable image (absolute, or relative to
  `setTextureDirectory()`, which the app sets to the project file's folder; working directory when empty)
  is loaded with QImage, cached by absolute path, resampled to 1024², and uploaded as a layer of one
  mip-mapped `GL_TEXTURE_2D_ARRAY` (max 16 textures; trilinear `textureGrad`). The image x axis follows the
  grain and covers 1000 mm; y covers 1000·h/w mm. Unreadable files / library keys: one warning per
  reference (`viewport` category) and the procedural grain is used.
* No feature lines in Realistic mode; selection highlight and section caps work as in the other modes.
* Frame time (llvmpipe, 2 004 parts, 1280×800, orbit frame incl. readback, same run on a loaded
  4-core machine): Realistic **≈ 57 ms**, ShadedWithEdges **≈ 58 ms** — no measurable cost
  (`ViewportGl.RealisticModeScreenshotAndFrameTime`; FOS_VIEWPORT_REALISTIC_SCREENSHOT saves the fixture).

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
