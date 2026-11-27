<!-- SPDX-License-Identifier: AGPL-3.0-only -->
# 8. Cutting and nesting

Open the **Cutting** tab. The layouts come from a **heuristic** optimiser: every layout is checked independently
(no overlaps, inside the sheet) but is **not guaranteed optimal**. Commercial optimisers may need fewer sheets.

## Reading the summary

One row per material **and method**:

* **Saw (guillotine)**: rectangular parts for the panel saw: pieces, sheets, area-based lower bound, utilisation and
  **Layout check** ("valid" or "problems: N").
* **Router nesting (heuristic)**: shaped parts (see below).

Select a row, then a **Sheet** ("Saw sheet 1 of 3", "Nest sheet 1 of 1") to see its layout.

## Saw settings

1. **Applies to**: **Project defaults** or one material.
2. **Kerf (blade width)**, **Trim left / right / top / bottom** (edge trim of the sheet).
3. **Objective**: **Fewest sheets**, **Least waste**, **Use remnants first** or **Fewest cuts**.
4. Press **Apply and re-optimise**.

**Use project defaults for this material** removes a material's own settings. Each change is undoable.

## Remnants (offcuts as extra stock)

For one material (not for "Project defaults"):

1. Choose the material under **Applies to**.
2. **Add remnant**: label, length (along the grain), width, quantity.
3. **Apply and re-optimise**. Remnants are used before full sheets.

**Remove remnant** deletes a row. A remnant of width 0 or other invalid values are refused.

**Workshop remnant store (command line only).** A shared offcut store across projects (`.fosremnants`) is managed
with `fos remnants`: `list`, `add`, `remove`, `import-offcuts` (keeps the offcuts of a project's plans, default at
least 300 × 100 mm) and `use` (cuts a project using stored boards first; dry run unless `--commit`). See
`fos remnants --help`.

## Router nesting for shaped parts

Parts with a non-rectangular outline (rounded corners, arcs, cut-outs that make them non-rectangular) are not sawn.
They are listed as **Routed parts** with their blank size and nested separately on their own sheets.

**Router nesting settings (all materials)**: **Tool diameter** (parts are spaced by it), **Clearance**, **Chord
tolerance**, **Margin left / right / top / bottom**, **Iterations**, **Seed** (same seed = same layout). Press
**Apply and re-nest**.

Shaped and sawn parts of the same material never share a sheet. Nesting quality has not been tested against
benchmarks.

## Exports

Cutting plans are included in the job pack ([chapter 7](07-production.md#job-pack-everything-in-one-document)) and
in the production batch window (**Export cutting plans (SVG)…**). From the command line: `fos cut`.
