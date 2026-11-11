# Known limitations

Honesty rule (ARCHITECTURE.md 4.7): nothing is labelled as working unless it is implemented and tested. This list starts with limitations that follow from the architectural decisions and will be extended by the integration lead.

## Geometry and drawings
* Hidden-line removal is rule-based and exact **for box-shaped parts only** (ADR-0002). Curved, tilted or free-form parts are not supported by drawings/HLR.
* No STEP import/export. The optional Open CASCADE backend is planned, not implemented. Exchange is limited to the neutral formats listed in `docs/research/interoperability.md`, as and when implemented.
* 2D offset/boolean operations beyond simple cases are not available until an in-house implementation or Clipper2 is added.

## Dimensions and data
* Part sizes are finished sizes; cut size subtracts edge-band thickness only. **No pre-milling or trimming allowance** is modelled (ADR-0004).
* Rounding: values are not rounded internally; displayed sums may differ from displayed lines by 0.1 mm (ADR-0003).
* **Hardware dimensions and drilling patterns are generic and unverified**; they are not taken from manufacturer data and are flagged as unverified. Check against the manufacturer's documentation before production.
* Edge-band and material libraries contain example data, not vendor-verified catalogues.

## Cutting and nesting
* Cutting and nesting use **heuristics, not optimal solvers**; results can be worse than commercial or exact solutions. Layouts are validated independently, but optimality is not claimed.
* True-shape nesting is not yet specified as complete; quality is untested against benchmarks.

## CAM
* **No verified CNC postprocessor.** No output has been run on a physical machine. Any generated machine code must be validated and reviewed by a qualified operator (`docs/research/cam-and-postprocessors.md`).
* Proprietary machine formats (Homag, Biesse, SCM and others) are not supported.

## Project files and plugins
* `.fosp` checksum is an integrity check, not a security signature (ADR-0006). Atomic save behaviour is a design decision; its test status must be checked per release.
* Plugins: only declarative data plugins are planned for phase 1; code-executing plugins are not supported (ADR-0007, status Proposed).

## Licensing
* `LICENSE` holds the AGPL-3.0 text sourced from the **SPDX licence list**, whose line wrapping differs from the text published on gnu.org. It is **to be byte-compared with the gnu.org text when gnu.org is reachable**; until then the legal text equivalence is not verified.
* Qt module licences and OCCT licensing details are recorded as unverified in `THIRD_PARTY_LICENSES.md`; a legal review is outstanding.

## Interoperability
* No import/export with bCAD, Astra, PRO100, GibLab or ViyarPro is implemented; their formats are unverified (`docs/research/interoperability.md`).
