# ADR-0008: Contoured parts and curve representation

Status: Accepted (2026-11-16)
Extends: ADR-0002 (geometry kernel), ADR-0004 (finished vs cut dimensions)

## Context

Every part was a box: `Part::length × width × thickness`. Real furniture needs non-rectangular flat
parts — rounded or arched tops, curved shelves, worktops with sink and hob cut-outs, shaped side panels,
decorative fronts. These parts are cut as a rectangular blank, routed on a CNC (or by hand), edge-banded
along curved edges, nested on sheets, drawn with radii, and validated for interference. The nesting
module already handles polygons, but nothing in the model could describe such a part, and CAM could not
route a contour.

## Decision

1. **Representation.** A part may carry an optional `PartShape` (`fos/domain/part.hpp`): an outer
   contour, through cut-outs, and one optional edge band per outer segment. Contours are closed rings of
   lines and circular arcs in the DXF LWPOLYLINE *bulge* convention (`fos/geometry/contour.hpp`), the
   de-facto exchange format of furniture CAD and CNC software. Arcs are exact; polygons are produced only
   by explicit tessellation with a chord tolerance (rendering, nesting, polygon validation).
2. **Frame and size invariant.** The outline lies in the part frame's XY plane and its exact bounding box
   is `[0, length] × [0, width]`. `length`/`width` therefore stay meaningful (finished bounding size) for
   every consumer that only needs the envelope, and placements are unchanged.
3. **Finished vs cut (generalises ADR-0004).** The outer contour is the FINISHED outline. The raw outline
   is the finished contour offset inwards by each segment's band thickness — lines stay lines, arcs keep
   their centre and change radius — computed only by `domain::cutContour()`. `domain::cutSize()` is the
   bounding size of that raw outline; for rectangles both reduce exactly to the ADR-0004 formulas, so no
   existing number changes.
4. **Edges.** A shaped part uses per-segment bands (`outerEdges`); `Part::edges` must then be empty.
   Cut-outs are not banded in this version.
5. **Manufacturing route.** Parts whose outline is not a plain rectangle are produced from a rectangular
   blank (raw bounds plus a routing allowance) and routed; for sheet optimisation they go to nesting with
   their tessellated raw outline, rectangular ones keep the guillotine optimiser.

## Alternatives considered

* **NURBS / general B-spline curves now.** Needed eventually for free-form design (phase 13), but furniture
  manufacturing data is overwhelmingly lines and arcs, CNC controllers interpolate lines and arcs natively,
  and exact offsets of arcs are arcs. Splines can be added later as a further segment kind; consumers
  already go through `segmentInfo()`/`tessellate()`.
* **Polygons only.** Simple, but loses exact radii (drawings must dimension R 50, CAM must emit G2/G3,
  edge-band lengths must be exact) and makes offsets approximate.
* **A separate "shaped part" entity.** Would duplicate the part model and every consumer; rejected in
  favour of an optional field on `Part` with the bounding-box invariant.

## Consequences

* Every consumer of parts must handle `shape`: drawings (outline, radii), viewport (extrusion with holes),
  validation (exact outline interference), production (area, banding length along the contour), cutting
  vs nesting routing, CAM contour toolpaths, formats/project codecs, furniture generation, UI.
* `fos::geometry` gains exact line/arc operations (intersection, offset, area, bounds, tessellation).

## Revisit when

Free-form surfaces or non-planar parts are needed (phase 13), or a consumer requires spline segments.
