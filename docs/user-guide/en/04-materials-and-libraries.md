<!-- SPDX-License-Identifier: AGPL-3.0-only -->
# 4. Materials, edge bands, hardware and the library editor

Every project carries its own **libraries**: materials (boards, glass), edge bands and hardware. Cabinets refer to
library entries by id. A new project starts with **generic placeholder entries**: no manufacturer, cost 0 (=
unknown), example sizes. Replace them with your real stock before quoting or producing.

## Opening the library editor

**Project ▸ Libraries…** opens the **Project libraries** window with three tabs: **Materials**, **Edge bands**,
**Hardware**. The list shows every entry; the tooltip "Used by" names the items that use it.

Buttons: **New**, **Duplicate** (creates `<id>-copy`), **Delete**, **Apply**. Every applied change is one undo step
in the main window. Invalid values are refused with the reason.

## Materials

1. **Materials** tab ▸ **New** (or **Duplicate** an existing board).
2. Fill in: **Id**, **Name**, **Manufacturer**, **Code**, **Supplier**, **Kind**, **Thickness**, **Sheet length**,
   **Sheet width**, **Grain** (visible grain along the sheet length), **Colour**, **Available**.
3. Prices: **Cost per sheet** and/or **Cost per m²** (0 = unknown). The quote uses cost per m² when boards are priced
   by area, and cost per sheet when it uses the cutting plans ([chapter 9](09-quotes.md)).
4. **Density** (kg/m³, 0 = unknown): needed for package weights in the packing list. Weights are never estimated.
5. **Apply**.

To use the material: select the cabinet ▸ **Construction** tab ▸ **Materials**, or a part ▸ **Override for this
part**.

## Edge bands

Fields: **Id**, **Name**, **Manufacturer**, **Code**, **Kind**, **Thickness** (added to the panel edge; the cut size
is reduced by it), **Tape width** (must cover the board thickness), **Colour**, **Cost per metre**.

## Hardware

Fields you can change: **Id**, **Name**, **Manufacturer**, **Code**, **Unit cost**.

* **Kind and drilling geometry come from the built-in catalogue and are not edited here.** To add a fitting,
  **Duplicate** a definition of the same kind and rename it.
* **Dimensions** shows whether the entry is "Verified against the manufacturer's data sheet" or "Not verified
  (generic or unchecked dimensions)". All built-in fittings are **not verified**.

> **Safety:** generic hinges, slides, lift mechanisms, sliding tracks, hangers, rails and legs have drilling
> positions that are *not* taken from any manufacturer. Before drilling, check every position against the real
> fitting's data sheet ([chapter 13](13-limitations-and-safety.md)).

## Moving libraries between projects

* **Library ▸ Export library file…** saves all entries as a `.foslib` file.
* **Library ▸ Import library file…** reads a `.foslib`. When an entry with the same id but other data exists, choose
  **Keep existing**, **Replace existing** (changes every item that uses it) or **Add as copies** (new ids
  `<id>-imported`). The import is one undo step.

Tip: set up one "workshop" project with your real boards, bands and prices, export it once, and import that file
into every new project.

## Notes

Delete is refused while an entry is in use. Library names are data and are not translated.
