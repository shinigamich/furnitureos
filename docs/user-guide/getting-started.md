<!-- SPDX-License-Identifier: AGPL-3.0-only -->
# Getting started with FurnitureOS

This guide describes the desktop application as it exists today. Everything mentioned here is implemented;
features that are not mentioned are not available yet (see `src/app/README.md`, "Limitations").

## 1. Build and start

Prerequisites (Ubuntu 24.04):

```bash
sudo apt install g++ cmake ninja-build nlohmann-json3-dev libgtest-dev libgmock-dev \
     qt6-base-dev libqt6opengl6-dev qt6-svg-dev qt6-tools-dev qt6-l10n-tools libgl-dev xvfb
```

Build and run:

```bash
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure      # the GUI smoke test runs under xvfb-run
./build/src/app/furnitureos                     # optionally: ./build/src/app/furnitureos kitchen.fosp
```

The language follows the system locale (Ukrainian or English). Change it in **Edit ▸ Preferences** and restart.
Messages from the engineering core in the Diagnostics panel are always English.

## 2. Your first cabinet

1. **File ▸ New from template** and pick a template: base cabinet, wall cabinet, tall cabinet, drawer unit,
   wardrobe section or open shelving.
2. Enter width, height (including the plinth) and depth in millimetres. All other parameters keep the template
   defaults. An impossible size is refused with the reason.
3. The cabinet appears in the **3D** workspace and in the **Project browser** (cabinet → assembly → parts with
   their finished sizes L × W × T). Clicking a part in either place selects it in both.
4. More cabinets: **Edit ▸ Add cabinet** places a new one to the right of the existing ones;
   **Edit ▸ Duplicate item** (Ctrl+D) copies the selected one; **Edit ▸ Delete item** removes it.

3D view: View ▸ View presets (front, back, left, right, top, bottom, isometric), perspective / orthographic,
shaded / shaded with edges / wireframe, Fit all (F). Mouse handling is that of the viewport module.

## 3. Editing parameters

Select the cabinet (not a part). The **Properties** panel shows the cabinet name and three tabs:

* **Parameters**, grouped by kind (lengths in mm, counts, angles, …). Type a number (`900`, `812.5`, `812,5`)
  or an expression over other parameters (`depth + 140`, `width / 2`) and press **Enter**.
  Derived parameters (in italics) are normally computed from others; typing a value **pins** them, and
  **Reset** restores the standard formula. Use the filter box to find a parameter by name.
* **Construction**: top and bottom joints, top panel or rails, back mounting, plinth, fasteners, shelf-pin holes;
  **Materials**: carcass, back and front materials and the edge bands of visible, hidden and front edges, chosen
  from the project library; **Hardware**: the hinge, handle, shelf pin, confirmat screw, dowel and foot used by
  the cabinet, each chosen from the fittings of that kind in the project's hardware library.
* **Interior**: sections, shelves and fronts (next section).

If an input is not accepted (unknown name, syntax error, a size the cabinet cannot be built with), the reason is
shown in red under the field and **nothing changes**. Every accepted change is one step in **Edit ▸ Undo /
Redo** (Ctrl+Z / Ctrl+Shift+Z); the menu names the step, e.g. "Undo Set width of Base cabinet to 900".
All views — 3D, browser, drawings, production and cutting — are regenerated after each change and after undo.

## 4. Interior: sections, shelves and fronts

The **Interior** tab shows the cabinet's sections as a tree: "Whole interior" at the top, below it the sections
of each split ("Section 1 (left)", "Section 2 (right)" for a vertical split, "(bottom)" / "(top)" for a
horizontal one), with each section's size and content. Select a section, then:

* **Split vertically** (side-by-side sections separated by partitions) or **Split horizontally** (stacked
  sections separated by fixed shelves) into the number of sections chosen under "Into sections" (2–9). The
  sections get equal shares; shelves of the split section move into its first section, its front stays.
* **Remove split** turns a split section back into one empty section (its front is kept). **Empty interior**
  replaces everything by one empty section without shelves or front.
* **Size** of a section within its split, then **Set size** (or Enter): `300` = fixed 300 mm, `1*`, `2*` =
  share of the remaining space, anything else = an expression over the parameters (`width / 3`). A size that
  does not fit is refused with the available space.
* **Shelves**: the number of adjustable shelves of a section that is not split (evenly distributed).
* **Front of the section**: no front, single door (hinged left or right), door pair or drawer stack; overlay
  (full, half, inset), cup hinges, handle position. For a drawer stack enter the drawer front heights from
  bottom to top separated by `;` with the same size syntax, e.g. `140; 1*; 1*`. Press **Apply front**.
* **Drawer boxes** (drawer stack only): "None (fronts only)" or "Box on slides". With a box, every drawer front
  gets a box behind it — two sides, inner front, back and bottom — carried by two **slides** (the drawer-slide
  entries of the project library: ball-bearing side-mounted or concealed undermount) with the **drawer bottom**
  in grooves or nailed under. The box size follows the opening, the cabinet depth and the slide rules (e.g. a
  600 mm wide cabinet with ball-bearing slides: box 538.6 mm wide). An impossible combination — a nailed-under
  bottom on undermount runners, a cabinet too shallow for the shortest slide — is refused with the reason and
  nothing changes. The box materials are chosen on the **Construction** tab: **Drawer boxes** (default: the
  carcass material) and **Drawer bottoms** (default: the back panel material).

Example: split the base cabinet vertically, give section 1 two shelves and keep the door pair on the whole
interior — the browser then lists a partition (684 mm long) and two 271 × 540 mm shelves in the left section.

## 5. A single part: facts and overrides

Selecting a part shows read-only facts: finished size, cut size (finished size minus edge band thickness),
material, grain, the four edges and the number of machining operations. For a cabinet part, **Override for this
part** sets its material (or "Cabinet material") and each edge L1, L2, W1, W2 ("Cabinet rule", raw, or an edge
band from the library). **Remove override** returns the material and edges to the cabinet's rules (a shape is
kept). Part sizes themselves always follow from the cabinet parameters and interior.

### Shaped parts (contours)

The **Shape** section (below the override for a cabinet part; in the inspector of a free panel) gives a part a
contoured outline. Choose the **outline**:

* **Rectangle** — no shape (cut-outs still possible);
* **Rounded corners** — a radius for each corner (L1 · W1 at (0, 0), L1 · W2 at (L, 0), …; empty = sharp);
* **Arc edge** — one edge (L1, L2, W1, W2) becomes an arc with the given **rise**, convex (cut into the part's
  box: its apex stays on the edge line) or concave;
* **Custom outline** — a vertex table (X, Y, bulge, band from) with **Add / Remove / Up / Down**; it starts from
  the full rectangle. Vertices run counter-clockwise; bulge 0 = straight, 1 = half circle, negative = clockwise.

**Cut-outs** (through openings, never banded): **Add rectangle** (X, Y = lower-left corner, length, width,
corner radius) or **Add circle** (X, Y = centre, radius). All values are mm in the part frame (X along the length,
Y along the width) and may be expressions of the cabinet parameters and `contour.length`, `contour.width`,
`contour.thickness` — e.g. a hole centred at `contour.length / 2`. **Apply** stores the shape (undoable);
an impossible shape (a radius that does not fit, a hole outside the part, a bad expression) is refused with the
reason under the buttons and nothing changes. **Reset** discards the edits in the section, **Remove shape**
returns to a rectangle. The part then shows "shaped part (routed)", its outline area, segment and cut-out count
and its blank (cut) size; the 3D view, the part drawings and the production tables (Shape, Outline area, Outline
edges, routing columns) show the real outline. Example: the open shelving template, shelf `shelf.1.1`, rounded
corners L1 · W1 and L1 · W2 = 30 → outline area 762 × 280 − 2 × (900 − 225π) ≈ 212 974 mm².

### Importing an outline from DXF

**File ▸ Import DXF outline…** adds a free panel whose outline comes from an ASCII DXF drawing (R12 or later;
lines, arcs, circles and polylines — explode blocks first; splines, ellipses and text are not imported and are
reported). Choose the file, then the panel's **name**, **material** (or "First material of the thickness below"
plus a **thickness**), an optional **layer**, the **units** (from the file; a file without units is read as mm)
and whether to **skip unsupported cut-outs**. The largest closed outline becomes the panel, closed outlines inside
it become cut-outs — circles and axis-parallel (rounded) rectangles; other shapes refuse the import unless they
are skipped. The panel is placed to the right of the existing items as one undoable step and its Shape section
shows the imported outline as a custom contour you can edit. A refusal (no closed outline, no material of that
thickness, …) is shown in the dialog with the DXF notes, and nothing is added; notes of a successful import
(e.g. "units assumed to be mm") are listed in a message box.

## 6. Drawings, annotations and exact-scale printing

The **Drawings** workspace shows the assembly drawing of each cabinet and, for the selected cabinet, a part
drawing of each part. Choose paper (A4–A1), orientation and scale ("Fit to sheet" picks the largest standard
scale that fits; a fixed scale that does not fit is refused with the required and available size). Several sheets
are listed under "Sheet". Zoom 100 % shows real paper size on a correctly calibrated screen.

Export: **PDF** (vector, exact scale, one page per sheet), **SVG** (one file per sheet), **DXF** (one file per
view, model space 1:1 in millimetres).

**Annotations** (panel to the right of the sheet). Click a dimension on the sheet to select it (it is
highlighted in blue). You can then change its **Offset** (paper mm; negative = below / to the left), replace its
**Text** (leave empty for the measured value), **Hide** it, or **Reset** it to the generated state. To add a
dimension, choose the direction, press **Pick two corners** and click near two part corners in the same view: the
dimension is attached to those parts and follows later size changes. All edits are stored in the project and
re-applied whenever the drawing is regenerated; they are undoable like any other change.

The panel lists the stored edits of the drawing. A replaced text whose dimension has changed since it was typed
is marked **STALE** and drawn with `*` on the sheet (e.g. "800 *"); an edit whose element no longer exists (for
example a dimension of a removed shelf) is marked **ORPHAN**. Both are explained under "Findings"; select an
entry and press **Remove edit** to delete it, or re-enter the text to confirm it.

**Print…** prints at exact scale (1 mm on the sheet = 1 mm on paper). If the sheet does not fit the printable
area, you are asked whether to print it fitted to the page instead; such a printout is stamped "not to scale"
with the factor. Every sheet carries a **100 mm calibration mark**: after printing, measure it with a ruler.
If it is not exactly 100 mm, the printer driver scaled the page (switch off "fit to page" in the driver); the
application cannot detect driver scaling.

## 7. Production documents

The **Production** workspace lists cut list, parts, materials, edge banding, hardware and machining, regenerated
from the current model. **Export table as CSV…** writes the shown table (UTF-8). Material sheet counts in this
workspace are area lower bounds; real sheet counts come from the Cutting workspace.

**File ▸ Export** also offers: cut list CSV, production sheet HTML (printable, for the selected or first
cabinet), part labels SVG (A4 sheets of 70 × 37 mm labels) and the 3D model as OBJ (millimetres, Z up, parts as
boxes, no machining).

## 8. Cutting

The **Cutting** workspace shows one row per material: pieces, sheets used, the area-based lower bound, the
utilisation and the result of the independent layout check. The layouts come from a **heuristic** optimiser:
they are valid (checked) but generally not optimal. Select a material and a sheet to see its layout.

**Saw settings**: choose "Project defaults" or a single material, set the kerf (blade width), the edge trim
on each side and the **objective** the optimiser compares layouts by (fewest sheets, least waste, use remnants
first, fewest cuts). For a single material you can also list **remnants** — leftover boards used before full
sheets (label, length along the grain, width, quantity): **Add remnant** / **Remove remnant**. Press **Apply and
re-optimise**; invalid values (e.g. a remnant of width 0) are refused with the reason. **Use project defaults for
this material** removes the material's own settings. Each of these is an undoable project change.

Shaped parts whose outline is not a plain rectangle are not cut on the saw: they are listed above the settings as
**Routed parts — nested separately** (name, material, blank size) and are not part of the saw layouts.

## 8a. Component library

A **component** is a cabinet or free panel saved together with the materials, edge bands and hardware it uses, so
it can be reused in any project. Components live in `.foscomp` files in library folders.

* **Components** dock (next to the project browser; View menu to show it): components by category, filtered by
  tag and a search text. Selecting one shows its description, size (width × height × depth, including fronts),
  author, tags and the materials / hardware it brings along.
* **Insert** (button, double click or context menu) adds the component to the project to the right of the existing
  items; Undo removes it again. If the project already has a *different* material, edge band or hardware entry
  with the same id, a dialog lists those ids and asks whether to keep the project's entries, replace them (this
  changes every item that uses them) or add the component's entries under new ids.
* **Edit ▸ Save as component…** (also in the dock's context menu) saves the selected item with a name, category,
  tags (comma-separated), description and author into a library folder. An existing file is only replaced after
  you confirm.
* Library folders are set in **Edit ▸ Preferences ▸ Component folders**. With an empty list, your personal folder
  in the application data directory is used (it is created automatically).

## 9. Saving, backups and recovery

* **File ▸ Save / Save As** write a `.fosp` file atomically (a crash never leaves a half-written file) and keep
  rotating backups `<file>.bak1` … (3 by default). The window title shows `*` while there are unsaved changes;
  closing asks whether to save.
* **Autosave** (Edit ▸ Preferences, default every 5 minutes, 0 = off) writes `<file>.autosave` while the project
  has unsaved changes; projects that were never saved are autosaved in the application data folder.
* **Recovery**: when you open a file and a newer autosave or backup exists, you are asked whether to recover it.
  The recovered state is one undoable step — Undo returns to the saved file. At start-up the application offers
  the autosave of a project that was never saved.
* A damaged or newer-version file is refused with the exact reason; it is never partially loaded.
