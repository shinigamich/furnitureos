# ADR-0004: Finished versus cut dimensions

* Status: Accepted
* Date: 2026-11-11

## Context

A panel is cut from sheet material at the saw and then edge-banded. Band thickness adds to the finished size. Designers think in finished (assembled) sizes; the saw needs cut sizes. The two must never be mixed in one field.

## Decision

`domain::Part::length` and `Part::width` are **finished** sizes, including the thickness of applied edge band. The saw (cut) size is derived **only** by `domain::cutSize(part, model)` (see `src/domain/include/fos/domain/model.hpp` and `part.hpp`):

```
cut.length = length - thickness(W1) - thickness(W2)
cut.width  = width  - thickness(L1) - thickness(L2)
```

Edge naming (from `part.hpp`): `L1` is y = 0 and `L2` is y = W; both run along the length, so banding on them narrows the cut **width**. `W1` is x = 0 and `W2` is x = L; both run along the width, so banding on them shortens the cut **length**. A raw edge contributes 0. An edge referencing an edge band missing from the model also contributes 0, and validation reports the missing reference. Thickness is the band thickness from the `EdgeBand` library entry.

Consequences of the convention that other modules must respect:
* Drawings, assembly geometry and collision checks use finished sizes.
* Cut lists, cutting optimisation and CAM nesting use `cutSize()`.
* Edge-face operation coordinates refer to the finished part (`operation.hpp`), so machining positions on banded edges are consistent with the assembled product. Operations on the A/B faces use coordinates in the finished frame; a CAM exporter working on the cut panel must translate by the band thickness on L1/W1 (this translation belongs to `cam`, not to the model).
* Grain is relative to the part frame; swapping length and width on the sheet is allowed only if `GrainDirection::None`.

## Alternatives considered

* **Store cut size, derive finished size.** Used by some software and common in shops that work from cut lists. Rejected as the primary field because changing band material would silently change the assembled size or require manual recalculation; designers specify finished sizes.
* **Store both and keep in sync.** Rejected: two sources of truth.
* **Per-edge allowance only on the edgebander.** Industry practice (not verified for specific vendors in this session): edgebanders with pre-milling remove a small amount (typically a fraction of a millimetre) from the edge before gluing, and trim flush afterwards. A cut size therefore may need to be `finished - band + preMill` on that edge so that the pre-milled panel reaches finished size. The current rule ignores this.

## Adding a pre-milling allowance later

* Add `double preMillAllowance` (mm, default 0) per edge-band record or per machine profile (not per part), as a library property of the edge treatment.
* Change `cutSize()` in one place: `cut.length = length - sum over W1,W2 of (thickness - preMill)` (clamped so allowance never makes the term negative; validation warns on unusual values).
* Add a regression test with allowance 0 proving existing results do not change; bump `.fosp` `formatVersion` with a migration that writes 0 for existing band records (ADR-0006).
* Cut list headers should state which convention and allowance were applied.

## Consequences

* Single, testable rule; edge-band changes propagate to cut sizes automatically.
* Shops expecting cut sizes only must read the cut list, not part dimensions; documentation must state this.
* Thickness accuracy of real band (glue line, trimming) is not modelled.

## Revisit when

* Pre-milling, trimming or glue-line allowances are required.
* Users of imported projects bring cut-size-based data (an importer would convert cut to finished using the rule above).
* Non-rectangular parts need a contour-based equivalent of the cut size.
