# ADR-0003: Units and numerical precision

* Status: Accepted
* Date: 2026-11-11

## Context

Furniture dimensions are given in millimetres, usually with 0.1 mm or 1 mm resolution, machining needs sub-0.1 mm accuracy, and parametric expressions (divisions, ratios, trigonometry) produce non-terminating values. The model feeds drawings, cut lists and CAM, so rounding policy must be single and explicit.

## Decision

* Lengths are `double` **millimetres**; angles are **degrees**. No other unit appears in the domain model (UI/CLI may convert at the boundary).
* Comparisons use `core::kLengthEps = 1e-6 mm` (absolute tolerance). Angle tolerance is defined in `core` alongside it.
* **No intermediate rounding.** Values are rounded only for display and reports, via `core::formatLength` with a default of **0.1 mm**.
* Quantities that must be physically realisable (cut sizes, hole positions in CAM output) are rounded once, at the output boundary, using a rounding rule stated by that output (machine profile or report).
* Equality of lengths in logic is `|a-b| <= kLengthEps`, never `==`. Tolerances are never scaled silently.

## Alternatives considered

| Option | For | Against |
|---|---|---|
| Integer micrometres (int64) | Exact addition/subtraction, deterministic hashing, no drift | Trigonometry and divisions still need rounding at every operation; overflow handling; awkward for rotated geometry and parametric expressions; conversions everywhere; Clipper2-style integer scaling is acceptable only locally. |
| Fixed-point decimal (0.01 mm) | Matches shop-floor resolution | Same problems; loses accuracy for CAM arcs and large assemblies. |
| `long double` | More precision | Not portable (MSVC treats it as double), slower. |
| Rationals / exact arithmetic | Exact predicates | Expensive; only needed in a few predicates (could be added locally, e.g. CGAL-style filtered predicates). |
| Round every stored value to 0.1 mm | "What you see is what is stored" | Accumulating rounding errors in derived values; breaks parametric consistency (two panels that should meet differ by 0.1 mm). |

## Floating-point pitfalls and mitigations

* Binary representation: 0.1 mm is not exactly representable; sums such as 18 + 0.1 + ... differ from the decimal result by about 1e-15 relative. Eps 1e-6 mm is about nine orders of magnitude above double rounding at furniture scale (up to about 1e4 mm), so it absorbs round-off while staying far below any physical tolerance.
* Catastrophic cancellation when subtracting nearly equal large numbers (e.g. placement coordinates) is acceptable at this scale; avoid squaring distances for tolerance tests (compare using the length, not its square, with eps).
* Transitivity: tolerance equality is not transitive; do not use it as an ordering key or hash key. Sort and group using explicitly rounded keys (for example to 1e-3 mm) when grouping identical parts, and document the rounding.
* Rounding display values: half-way cases (e.g. 0.05) may round either way depending on representation; reports must use a single formatting function so totals agree with lines. Where lines are rounded and totals computed from unrounded values, state this in the report.
* Accumulated area/volume sums use plain `double`; order-dependence is below reporting resolution, but tests must use tolerances, not exact equality.
* Compiler flags: no `-ffast-math`; avoid x87 extended precision (use SSE2/AVX on x86-64, which is default there).
* Serialisation: JSON numbers are written with enough digits to round-trip a double (17 significant digits or shortest round-trip form), so save/load is lossless.

## Consequences

* Simple model code; no unit types needed internally (a `Length` strong type may be added later without changing the convention).
* Users may see 0.1 mm discrepancies between rounded displays and sums; documented behaviour.
* CAM and exchange formats with integer or fixed resolution round at the boundary and must report the rounding (see ADR-0004 for cut sizes).

## Revisit when

* Inch-native users require inch display (display conversion only; the model stays in mm).
* An exact-predicate need appears in HLR or nesting that eps cannot satisfy.
* Models with extents large enough that 1e-6 mm approaches the double resolution (about 1e9 mm; not realistic for furniture).
