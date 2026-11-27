<!-- SPDX-License-Identifier: AGPL-3.0-only -->
# 6. Drawings

Open the **Drawings** tab. Drawings are regenerated after every change to the model.

## Choosing a drawing

The **Drawing** list offers, for each item:

* **— assembly drawing**: front view, top view below and left view to the right (ISO first angle) with automatic
  dimensions;
* **— isometric view**: pictorial view; **View from** front left or front right; **Balloons** adds part numbers (as
  in the part list) in circles with leaders;
* **— exploded view**: like the isometric view, with **Explode** factor (1 = not exploded; 1.5 = moving parts travel
  half the assembly size along their axis);
* for the selected item, one **part drawing** per part (indented).

## Sheet settings

* **Paper**: A4–A1, **Landscape** or **Portrait**.
* **Scale**: **Fit to sheet** (largest standard scale that fits) or a fixed scale 1:1 … 1:50. A fixed scale that
  does not fit is refused with the required and available size, never silently changed.
* **Sheet**: when a drawing needs several sheets.
* **Zoom**: 100 % = real paper size on a correctly calibrated screen.

## Editing dimensions (annotations)

The panel to the right of the sheet:

1. Click a dimension on the sheet (it turns blue).
2. Change **Offset** (paper mm; negative = below / left), replace the **Text** (empty = measured value), **Hide** it,
   or **Reset** it.
3. To add a dimension: choose horizontal or vertical, press **Pick two corners**, click near two part corners in the
   same view. It follows later size changes.

All edits are saved in the project and are undoable. A replaced text whose measured value has changed is marked
**STALE** and drawn with `*` (e.g. "800 *"); an edit whose element no longer exists is **ORPHAN**. Select it and
press **Remove edit**, or re-type the text.

## Export

Buttons in the workspace or **File ▸ Export**:

| Format | Result |
|---|---|
| **PDF…** | Vector PDF, exact scale, one page per sheet |
| **SVG…** | One SVG file per sheet |
| **DXF…** | One DXF file per view, model space 1:1 mm (assembly and part drawings only; pictorial views export as SVG or PDF) |

## Printing at exact scale

1. **File ▸ Print drawing…** (Ctrl+P) or **Print…** in the workspace.
2. In the printer dialog **switch off "fit to page" / scaling**.
3. If the sheet does not fit the printable area, you are asked whether to print it fitted to the page; such a print
   is stamped "not to scale" with the factor.
4. **Measure the 100 mm calibration mark** on the printout with a ruler. If it is not exactly 100 mm, the printer
   driver scaled the page. The program cannot detect driver scaling.

Limitation: hidden lines are exact only for box-shaped parts; a shaped part hides other parts as its bounding box.
