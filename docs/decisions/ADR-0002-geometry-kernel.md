# ADR-0002: Geometry kernel

* Status: Accepted
* Date: 2026-11-11

## Context

The primary workload is panel furniture: axis-aligned rectangular panels (boxes) placed by rigid transforms, 2D contours (cut-outs, pockets, nesting shapes), prisms (extruded contours) and drawings with hidden-line removal (HLR). Cabinets are built from flat sheet material; free-form solids (curved handles, imported hardware models, upholstered shapes) are secondary. Exchange with other CAD systems usually means STEP, which requires a B-rep kernel.

Facts about candidate libraries below are from general knowledge unless marked. Only the items marked "verified" were checked online in this session (GitHub pages only; the OCCT and Qt web sites were not reachable).

## Decision

1. The `geometry` module is **FurnitureOS' own lightweight kernel**: vectors, rigid transforms, AABB, 2D polygons, prisms, meshing of boxes/prisms, intersections, and **exact rule-based HLR** for box-shaped parts (silhouette and hidden edges derived analytically from the box faces, not from a rasterised or tessellated view).
2. **Open CASCADE Technology (OCCT)** is a planned **optional backend** behind a `geometry` interface for STEP import/export and free-form solids. It is not a core dependency: the project builds, tests and runs all furniture workflows without it. It would be enabled by a CMake option and shipped as a separate, clearly labelled component.
3. 2D polygon offset/boolean operations needed by nesting and CAM (kerf/tool-radius offsets, cut-outs) will use either an in-house implementation for simple cases or **Clipper2** (BSL-1.0) if the in-house version proves insufficient. No decision to add Clipper2 is taken yet.

## Alternatives considered

### Open CASCADE Technology
* Licence: LGPL-2.1 with the "OCCT exception" (file `OCCT_LGPL_EXCEPTION.txt`); a commercial licence is offered as an alternative (verified on the GitHub README; the OCCT site itself not reachable). Any claim that a newer major version changed the licence: not verified in this session. Check the tag you actually adopt.
* Pros: full B-rep, STEP/IGES/STL/OBJ I/O, booleans, fillets, HLR algorithms, meshing, long track record.
* Cons: very large build (many toolkits; hours-scale on a laptop for a full build, not measured here), large API with its own handle/memory model and macros, steep learning curve, binary size, LGPL relinking obligations on top of Qt's, historically exceptions-based error handling. Dynamic linking is the practical way to comply with LGPL. Boolean/HLR robustness on thin panels with coincident faces needs fuzzy tolerances and still can fail.
* Maintenance: actively developed (the repository references a version 8.0 documentation line; the exact current release was not verified).
* Fit: excellent for STEP and free-form; disproportionate as the heart of a box-panel program, and it would make "headless, fast, small" build goals harder.

### CGAL
* Licence split: core packages LGPL-3.0-or-later, many (e.g. 2D/3D triangulation, Polygon Mesh Processing, Nef polyhedra, convex hull in some versions) GPL-3.0-or-later. Details differ per package; not re-verified here.
* Pros: robust exact predicates and constructions; good for polygon booleans and Minkowski sums.
* Cons: header-heavy templates, long compile times, Boost dependency. GPL-3.0-or-later packages can be combined with an AGPL-3.0 program (GPLv3 section 13 and AGPLv3 section 13 allow the combination), but the GPL packages would make the CGAL part non-LGPL and need per-package licence tracking. Acceptable technically (compatibility to be confirmed by a licence review); not necessary now.

### Manifold
* Licence: Apache-2.0 (from memory; not verified).
* Pros: fast, robust triangle-mesh booleans, small.
* Cons: mesh-only (no STEP, no exact B-rep, no analytic HLR); useful only if mesh booleans for visual collision or preview become important.

### Clipper2
* Licence: BSL-1.0 (verified on GitHub). Features: union, intersection, difference, XOR, polygon offsetting (verified).
* Status: the Angus Johnson repository is shown as archived with maintenance moving to the `PolyClipping/Clipper2` repository (verified on the GitHub page); confirm the active fork before adopting.
* Uses integer coordinates internally (scaled doubles). Scaling to micrometre-level integers conflicts conceptually with ADR-0003 (no intermediate rounding), so quantisation must be explicit and confined to the call site.
* Fit: strong candidate for 2D offsets and booleans in `nesting`/`cam`.

### Other
* Build everything on meshes only: rejected, loses exactness needed for dimensions and drawings.
* Use SolveSpace's kernel or other GPL kernels: licence compatible with AGPL but heavy coupling and a different parametric model.

## Consequences

* Core stays small, fast and testable; exact HLR for boxes is simple and verifiable by closed-form tests.
* Non-box parts (curved, free-form) are not supported by drawings/HLR until the OCCT backend or an extension of the kernel exists (see `KNOWN_LIMITATIONS.md`).
* No STEP in the first releases. Users needing STEP use mesh/DXF/SVG export meanwhile.
* An abstraction boundary for the optional backend has to be designed before implementation; it is not yet specified.
* An OCCT build adds LGPL-2.1 (+exception) obligations; see `THIRD_PARTY_LICENSES.md`.

## Revisit when

* STEP import/export or non-box parts become a committed requirement.
* Prism/contour boolean robustness in the in-house kernel produces repeated bugs (adopt Clipper2 or CGAL for 2D).
* Rule-based HLR must handle curved or tilted panels.
* Clipper2 maintenance or OCCT licensing changes.
