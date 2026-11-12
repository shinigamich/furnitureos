# drawings

Renderer-independent technical drawings generated from a `domain::Model`.
Dependencies: `core`, `geometry` (only `vec.hpp`, `transform.hpp`, `aabb.hpp`), `domain`. No Qt, no JSON.
The Qt `printing` module paints `Sheet::primitives`; the app displays them and stores `DrawingEdits`.

## Pipeline

```
domain::Assembly / Part
   └─ generateAssemblyView / generatePartView      -> View            (model mm, 1:1, HLR done)
        └─ autoDimensionView / annotatePartView    -> ViewAnnotations (stable ids)
             └─ applyEditsToView (DrawingEdits)    -> ViewAnnotations (user edits re-applied by id)
                  └─ renderView (ViewPlacement)    -> Primitive[]     (paper mm)
                       └─ makeSheet + arrangement  -> Sheet           (frame, title block, calibration)
                            └─ toSvg / printing module;   modelSpacePrimitives -> toDxf
```
`generateAssemblyDrawing()` and `generatePartDrawing()` (`drawing.hpp`) run the whole chain.
The model is only ever taken by `const&`; nothing in this module can change it.

| Header | Contents |
|---|---|
| `geom2d.hpp` | `Rect2`, `Segment2`, Liang–Barsky clipping, `hatchRect` |
| `primitives.hpp` | `Line`, `Polyline`, `Circle`, `Arc`, `Text`, `Marker`, `Primitive`, line styles/weights/dashes, text box estimate |
| `view.hpp` | `ViewKind`, `ViewBasis`, `View`, `ViewPlacement`, `generateAssemblyView` (views and sections) |
| `hlr.hpp` | `removeHiddenLines` for axis-aligned boxes |
| `annotation.hpp` | `Dimension`, `Note`, `LeaderNote`, `DetailMarker`, `EdgeBandMark`, `GrainArrow`, `DimensionStyle`, `layoutDimension`, `autoDimensionView` |
| `edits.hpp` | `DrawingEdits`, `ElementEdit`, user annotations, `applyEditsToView`, `finishEdits`, `EditReport` |
| `render.hpp` | `DrawingStyle`, `renderView`, `renderDimension` |
| `part_view.hpp` | `generatePartView`, `annotatePartView`, `PartDrawingLabels` |
| `sheet.hpp` | paper sizes, margins, `Scale`, `ScaleRequest`, title block, calibration mark, `Sheet`, `makeSheet` |
| `drawing.hpp` | `generateAssemblyDrawing`, `generatePartDrawing`, `ProjectionMethod` |
| `svg.hpp`, `dxf.hpp` | exporters |

## Conventions

**Paper space**: millimetres, origin bottom-left of the sheet, Y up, angles in degrees counter-clockwise.
Line weights and dash patterns are paper mm. Writers flip Y.

**View coordinates** (model mm at 1:1): `u = p·right`, `v = p·up`, `depth = p·toViewer` (larger = nearer).
All bases are right-handed (`right × up = toViewer`):

| View | looks along | right | up | toViewer | (u, v) |
|---|---|---|---|---|---|
| Front | +Y | +X | +Z | −Y | (x, z) |
| Back | −Y | −X | +Z | +Y | (−x, z) |
| Left | +X | −Y | +Z | −X | (−y, z) |
| Right | −X | +Y | +Z | +X | (y, z) |
| Top | −Z | +X | +Y | +Z | (x, y) — front of the furniture at the bottom |
| Bottom | +Z | +X | −Y | −Z | (x, −y) |

View coordinates are not shifted to zero (the Left view of a 563 mm deep cabinet spans u ∈ [−563, 0]).
Part views use the part frame: face A `(u, v) = (x, y)`, face B `(x, −y)`.
`ViewPlacement{origin, scale}` maps view → paper: `paper = origin + view · scale`; `Sheet::views` keeps the
placement of every view so a UI can convert clicks back to view/model coordinates.

**Ids.** Geometry primitives carry `sourceId` (part / operation / edge-band id). Annotation primitives carry
`elementId`, the stable id of the editable element:

* views: `<assembly>/view.<front|back|left|right|top|bottom>`, `<assembly>/section.<kind>`, `<part>/view.faceA|faceB`
* `<view>/dim.overall.<width|depth|height>` (assembly), `<view>/dim.overall.<length|width>` (part)
* `<view>/dim.chain.<h|v>[<from>..<to>]`, boundaries `env:min`, `env:max`, `<part-id>:min`, `<part-id>:max`
  — named after the parts on both sides, so adding a shelf elsewhere does not rename existing dimensions
* `<view>/dim.hole.<x|y>[<operation-id>]`, `<view>/hole[<operation-id>].label`, `<view>/edge.<L1|L2|W1|W2>`,
  `<view>/grain`, `<view>/title`, `<view>/note.<material|finished|cutsize|grain>`,
  `<view>/legend.edge[<edge-band-id>]`, `<view>/legend.hole.H<n>`
* sheet furniture: `sheet/frame`, `sheet/titleblock`, `sheet/title.<field>[.label]`, `sheet/calibration.*`

## Hidden-line removal

Exact for panels whose placement is axis-aligned (every current furniture part). Each box contributes the four
edges of its near face and of its far face. A point of an edge at depth *d* is hidden when it lies strictly
inside (by more than 1e-6 mm) the projected rectangle of a box whose near face is nearer than *d* by more than
1e-6 mm. Edges are split into visible and hidden intervals; then all pieces lying on the same line are cut at
each other's end points, a visible piece wins over hidden ones, the nearest piece (then assembly order) gives
the `sourceId`, and neighbouring pieces with the same style and source are joined. Hidden lines are dashed or
omitted (`HiddenLineMode`).

**Section views** (`ViewOptions::sectionPosition`): the plane is perpendicular to the viewing direction at the
given assembly coordinate (vertical for Front/Back/Left/Right, horizontal for Top/Bottom). Parts entirely on the
viewer's side are removed, intersected parts are truncated at the plane and listed in `View::cuts`, parts behind
are projected with HLR. Hatch lines (alternating 45°/135° per cut part) are produced at render time, properly
clipped to the cut rectangle, with spacing in paper mm.

## Dimensions

`Dimension` = two anchors in view coordinates + orientation + signed paper offset. The value is always
`measuredValue()` (never stored or typed); `textOverride` only replaces the displayed string. Text height,
arrows, gaps and offsets are paper mm and independent of the scale.

Automatic layout (deterministic): horizontal dimensions below the view, vertical ones to the left; nearest to
the view a chain through the faces of all panels seen edge-on, then the overall dimension. Stack distances
follow `stackDimensionOffsets` (documented in `annotation.hpp`). Chain texts that would collide go to a second
(third, …) text row; the next dimension line moves out accordingly. Spans shorter than 2.5 arrow lengths get
outside arrows on a 1.6-arrow tail.

## Edits

`DrawingEdits` (one per drawing) holds `ElementEdit`s keyed by element id (offset override, text shift, hidden,
text override) and user annotations (text, leader, extra dimension, detail marker) bound to a view id. They are
re-applied on every regeneration. `EditReport` lists applied ids and **orphans** (ids that no longer exist) with
one warning each (`drawings.edits.orphan`, `drawings.edits.orphan_annotation`); orphaned edits are never
deleted by this module. Other codes: `drawings.edits.not_applicable`, `drawings.edits.duplicate_id`.

## Sheets and scale

A4/A3/A2/A1/custom, portrait/landscape, margins (default 20/10/10/10), border frame, 180 × 28 mm title block
(project, title, author, date, scale, sheet n/m; captions from `TitleBlockLabels`, date supplied by the caller),
calibration mark (100 mm line with ticks, or 100 × 50 mm rectangle) labelled with its true size plus a
caller-supplied note about printer scaling.

Scale is either explicit (any ratio; generation **fails** with the required and available size if the views do
not fit) or fit-to-page, which takes the largest of 1:1, 1:2, 1:5, 1:10, 1:20, 1:50, 1:100 that fits and
records it in `Sheet::scale` and the title block. No other scale is ever chosen silently.

Three-view arrangement: `ProjectionMethod::FirstAngle` (default, ISO/European): top view **below** the front
view, **left** view to the right. `ThirdAngle`: top view **above**, **right** view to the right. Neighbouring
views share the origin coordinate along the common axis (aligned projection). Each requested section goes on
its own sheet.

## Exporters

* **SVG**: `width="<w>mm" height="<h>mm" viewBox="0 0 <w> <h>"` → one user unit = 1 mm exactly; coordinates are
  flipped individually (`y_svg = h − y`), strokes and dashes in mm, text escaped UTF-8, `data-source` /
  `data-element` attributes.
* **DXF R12 (AC1009)**: model space, 1:1, mm; LINE / CIRCLE / ARC / TEXT on layers VISIBLE, HIDDEN, CENTER,
  DIMENSION, HATCH, TEXT; sections HEADER, TABLES (LTYPE, LAYER, STYLE), BLOCKS (empty), ENTITIES, EOF.
  Annotation sizes are multiplied by an annotation scale (default 10) so they are legible at full size.

## Limitations (honest list)

* HLR and section cutting handle axis-aligned boxes only. A rotated part is drawn with all 12 edges visible,
  hides nothing, is not cut, and is reported (`drawings.hlr.not_axis_aligned`). Machining (holes, grooves,
  cut-outs) and hardware are not shown in assembly views; panels are plain boxes there.
* No isometric/perspective views, no detail views (a `DetailMarker` only marks the region), no view captions.
* Dimensions are linear horizontal/vertical only (no aligned, angular, radial dimensions). Automatic
  dimensioning covers overall sizes and edge-on panel chains; set-backs (e.g. shelf 20 mm behind the front)
  are not dimensioned automatically — add a `UserDimension`.
* Text width is a **heuristic** (0.6 × height per code point); there are no font metrics in the core. Automatic
  layout guarantees no overlap between dimension texts/lines under that estimate; long notes and title-block
  values are not wrapped or clipped. Extension lines may cross other dimension lines (normal for stacked
  dimensions), and edge-band codes, hole labels and the grain arrow are not collision-checked (use text shifts).
* Part drawings: pockets on edge faces are not drawn (`drawings.part.unsupported_operation`); edge-face
  operations appear only as hidden outlines in the face view (no separate edge views); `quantity` is supplied
  by the caller.
* Fit-to-page only considers reduction scales down to 1:100 and fails beyond that.
* DXF: R12 has no `$INSUNITS`/`$MEASUREMENT`; they are written only on request
  (`DxfOptions::writeUnitVariables`). No POLYLINE/SOLID/DIMENSION/HATCH entities (polylines → LINEs, arrowheads
  → three LINEs), no line weights, non-ASCII text as `\U+XXXX`, characters beyond U+FFFF as `?`. Sheets
  (paper space) are not exported to DXF, only view geometry.
* SVG text uses a generic font family; exact glyph widths depend on the viewer.
