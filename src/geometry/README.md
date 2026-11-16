# fos::geometry — geometry kernel

Exact 2D/3D geometry for FurnitureOS (ADR-0002, extended by ADR-0008). Units: millimetres;
angles at the API in degrees. Every tolerance `eps` is a **distance** in mm (default
`core::kLengthEps` = 1e-6 mm). Expected failures are returned as `core::Result`; nothing throws
for bad input.

| Header | Contents |
|---|---|
| `vec.hpp`, `rect.hpp`, `aabb.hpp`, `transform.hpp` | vectors, 2D rectangles, 3D boxes, rigid transforms |
| `segment.hpp` | 2D segment predicates, intersection and closest points |
| `polygon.hpp` | polygons (area, winding, point location, simplicity, hull, relations) |
| `offset.hpp` | polygon offset (`offsetPolygon`) |
| `triangulate.hpp`, `mesh.hpp`, `ray.hpp`, `projection.hpp` | triangulation, display meshes, picking, drawing projection |
| `contour.hpp` | **closed line/arc contours** (ADR-0008, below) |
| `curve_intersect.hpp` | exact line/circle/arc intersections and distances |

## Contours (`contour.hpp`)

A `Contour` is a closed ring of `ContourVertex{point, bulge}` (first vertex not repeated).
Segment *i* runs from vertex *i* to vertex *(i+1) % n*; the bulge of vertex *i* describes
segment *i* (DXF LWPOLYLINE convention).

### Conventions

* **Bulge** `b = tan(theta/4)`, `theta` = signed included angle. `b = 0` line, `b > 0`
  counter-clockwise arc, `b < 0` clockwise arc, `|b| = 1` half circle, `|b| < 1e6`
  (a full circle needs two segments; `circleContour` uses two half circles).
  For a chord `c`: `radius = c (1 + b²) / (4|b|)`, sagitta `s = |b| c / 2` (so `b = 2s/c`),
  centre at signed distance `c (1 - b²) / (4b)` along the chord's **left** normal from the
  chord midpoint (it changes side for |b| > 1). Common bulges: 90° → `tan 22.5° = 0.41421356`,
  180° → 1, 270° → `tan 67.5° = 2.41421356`.
  Arcs whose sagitta is below 1e-12 mm are treated as lines by `segmentInfo()`.
* **Orientation**: `signedArea > 0` ⇔ counter-clockwise. All builders return CCW contours.
  `reversed()` keeps vertex 0 first; the bulge of new segment *k* is `-b[(n-k-1) % n]`.
* **`segmentInfo(c, i)`** gives start, end, `isArc`, centre, radius, `startAngle` (deg,
  centre → start), signed `sweep` (deg, > 0 CCW) and exact `length`.

### Functions

| Function | Notes |
|---|---|
| `validateContour(c, eps)` | ≥ 3 vertices (2 if a segment is an arc), finite values, `|b| < 1e6`, no segment shorter than eps, no self-intersection. Non-adjacent segments must stay farther than eps apart; adjacent segments may only share their common vertex (tangent continuations are fine; fold-backs and arcs curling back over a neighbour are rejected). Errors name the segment indices and the contact point. O(n²). |
| `signedArea`, `perimeter` | exact: shoelace + signed circular segments `r²/2 (θ − sin θ)`; arc lengths `r|θ|` |
| `bounds` | exact, includes arc extrema at 0/90/180/270° inside the sweep |
| `reversed`, `normalizedCcw`, `translated` | |
| `locatePoint(c, p, eps)` | `OnBoundary` within eps of any segment (exact arc distance); otherwise even-odd crossing test, arcs split at 90°/270° into y-monotone pieces |
| `tessellate(c, tol)` | per arc `n = ceil(|θ| / min(2 acos(1 − tol/r), 90°))` chords ⇒ sagitta ≤ tol; original vertices kept exactly, orientation preserved; `tol` is clamped to ≥ 1e-6 mm |
| `rectangleContour(L, W)` | (0,0),(L,0),(L,W),(0,W): segments 0..3 = bottom, right, top, left (= edges L1, W2, L2, W1 of a part) |
| `roundedRectangleContour(L, W, r[4])` | radii for corners (0,0),(L,0),(L,W),(0,W); starts at the end of corner 0's arc: bottom line, corner-1 arc, right line, corner-2 arc, top line, corner-3 arc, left line, corner-0 arc. Sharp corners (r = 0) and zero-length straights (radii filling a side) are omitted. Error if radii are negative or do not fit a side. |
| `circleContour(c, r)` | two CCW half circles starting at (cx + r, cy) |
| `offsetContour(c, d[i], eps)` / `offsetContour(c, d, eps)` | see below |

`curve_intersect.hpp`: `intersectLines`, `intersectLineCircle`, `intersectCircles` (infinite
primitives; tangency within eps reported as one point), `closestPointOnContourSegment`,
`distancePointContourSegment`, `intersectContourSegments` → `{None | Points | Overlap, points}`.

### Offset rules (`offsetContour`)

* **Sign**: `d > 0` outwards (away from the enclosed area), `d < 0` inwards, for either
  orientation. With `o = +1` for a CCW and `−1` for a CW contour, the outward normal of a
  segment with unit tangent `t` is `o · (t.y, −t.x)`.
* **Lines** move by `d` along that normal. **Arcs** stay concentric:
  `r' = r + d · o · sign(sweep)`. On a CCW contour a CCW (convex) arc has its centre inside,
  so outwards grows it (`r + d`); a CW (concave) arc has its centre outside, so outwards shrinks
  it (`r − d`). Example: rounded rectangle R 50 offset −2 → R 48, straights keep their length.
* **Joins** at each original vertex V between offset segments P (incoming) and Q (outgoing):
  1. *Tangent junction* (|sin| of the turning angle ≤ 1e-6, same direction):
     equal distances → the shared point `V + d·n`; different distances → each segment keeps
     its own offset over its own extent and a **straight step along the common normal** joins
     `V + d_P·n` to `V + d_Q·n`. (This is the physically correct raw outline when only some
     segments are edge-banded: e.g. banding only the straight bottom edge of a rounded
     rectangle gives 2 mm steps at the tangent points; trimming the unbanded arcs instead would
     remove material the straight band cannot replace.)
  2. Otherwise the exact intersection of the two infinite offset primitives (line/line miter,
     line/circle, circle/circle) **nearest to V**.
  3. No intersection, equal distances and V on the gap side (outwards at a convex corner or
     inwards at a concave one): **round join**, an arc of radius |d| about V
     (bulge `tan(α/4)`, α = signed turning angle).
  4. Anything else is an error.
  Inserted steps/round joins come right after the segment they follow, so output segment
  indices shift by the number of connectors inserted before them.
* **Errors (never a wrong contour)**: invalid input, wrong distance count or non-finite
  distances, an arc radius becoming ≤ eps, a segment vanishing or reversing (lines: direction
  check; arcs: new sweep must keep its sign and length > eps), an orientation flip, or a result
  that fails `validateContour` (self-intersection).

### Limitations

* Offsets are local: features that would vanish are reported as errors, not removed
  (no topology change, no splitting into several contours). Use a smaller offset or
  simplify the outline.
* Junctions within 1e-6 rad of tangency are treated as tangent (`V + d·n` uses the mean of
  the two normals; deviation ≤ |d|·1e-6 mm).
* `tessellate` guarantees the chord tolerance; the polygon is simple when no two features of
  the contour come closer than the tolerance.
* No splines/ellipses (ADR-0008: future segment kind); no boolean operations on contours.
