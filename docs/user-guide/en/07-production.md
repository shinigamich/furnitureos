<!-- SPDX-License-Identifier: AGPL-3.0-only -->
# 7. Production documents

All lists are generated from the current model; there is nothing to "update" by hand.

## The Production tab

| Table | Content |
|---|---|
| **Cut list** | Parts to cut per material with **cut size** (finished size minus edge band thickness) and grain |
| **Parts** | Every part with finished size, material, edges |
| **Materials** | Area per material and an area-based **lower bound** of sheets (real counts: Cutting tab) |
| **Edge banding** | Metres per edge band |
| **Edge-banding jobs** | Job list for the banding machine operator, see below |
| **Hardware** | Fittings and fasteners with quantities (linear fittings in metres) |
| **Machining** | Drilling, grooves and other operations per part |
| **Bought-in items** | Items ordered finished to size (glass panes), see below |

**Export table as CSV…** writes the table you see (UTF-8, opens in Excel / LibreOffice). Shaped parts get extra
columns (shape, outline area, routing length).

Note: there is **no pre-milling or trimming allowance**; cut size subtracts only the edge band thickness.

### Edge-banding jobs

Grouped by edge band; for each part the cut size and for each banded edge the edge length and strip length
(including overhang), with a suggested order (long edges first, then short: common practice, not a machine
requirement). Shaped parts are listed separately for contour banding. **Export CSV…** or **Export SVG sheets…**
(A4 per band).

### Bought-in items

Glass panes of glass-panel fronts are ordered cut to size and are therefore **not** in the cut list, materials or
edge banding; they are listed here by size and material.

## Part labels with barcodes

1. **File ▸ Export ▸ Part labels (SVG)…**
2. Choose **Barcode**: none or Code 128, and **Barcode content**: the part id, or project | part id.
3. Choose the file name. Labels are 70 × 37 mm, 3 × 8 per A4 sheet, one SVG file per page.

The barcode replaces the part-id text line. If a label is too narrow for a readable barcode it keeps the text and
you are told which ones. Print the SVG at 100 % and test-scan one label before printing a whole job.

## Production sheet

**File ▸ Export ▸ Production sheet (HTML)…**: a printable sheet for the selected (or first) item. Also **File ▸
Export ▸ Cut list (CSV)…**.

## Packing list and assembly sheets

Open the **Packing** tab (or **Project ▸ Packing list and assembly**).

1. Set the limits: **Max. weight** (panel weight per package), **Max. length**, **Max. width**, **Max. height**
   (0 = no limit) and **Padding** (packaging allowance on each side).
2. The **Packages** table groups each item's panels into packages (largest at the bottom); **Hardware bags** lists
   one bag of fittings per item.
3. On the **Packing list** page: **Export CSV…** and **Export package labels…** (A4, 70 × 37 mm).
4. The **Assembly** page shows the assembly sheets; **Export SVG…** saves them.

* Weights come **only** from the material **Density** in the library. Without it the weight is "unknown" and the
  package is marked. Packaging and hardware weight are not included.
* **Assembly sheets** give numbered steps per item (carcass ▸ back ▸ shelves and dividers ▸ drawer boxes ▸ fronts
  ▸ handles ▸ rest). The order is **generic**, derived from part roles, **not a verified instruction**.

## Job pack (everything in one document)

1. **File ▸ Export job pack…**
2. Tick the **Sections**: cover page, part list, cut list per material, bought-in items, edge banding, hardware,
   packing list, assembly sheets, cutting plans (warnings are added automatically when there are any).
3. Optionally tick **Open in the browser after export**, then **Export…**.

The result is one HTML file (A4 landscape, each section on a new page) to print from the browser. Cutting plans in
it are **heuristic** layouts, as in the Cutting tab.

## Several projects at once

To produce several orders together (one cut list, shared sheets, batch labels) use **File ▸ Production batch…**
([chapter 11](11-files.md#production-batches)).
