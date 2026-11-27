<!-- SPDX-License-Identifier: AGPL-3.0-only -->
# 13. Limitations and safety

FurnitureOS is a preview in early development. It labels nothing as working unless it is implemented and tested,
and it says clearly where data is generic. The full technical list is in `KNOWN_LIMITATIONS.md` in the program
folder / repository.

## Before you drill: check the hardware

* **All built-in hardware is generic and unverified**: hinges (including inset and half-overlay), drawer slides,
  lift mechanisms for flaps, sliding-door tracks, cabinet hangers, wall rails, clothes rails, desk legs, shelf pins,
  confirmat screws and dowels. Their sizes and drilling patterns are **not** taken from any manufacturer.
* **Check every drilling position against the real fitting's data sheet** before production, and drill a test
  piece first. The library shows each entry as "Not verified" unless it was marked verified.
* Fittings placed in a removed area of a shaped part are reported as an error, not moved.

## Before you cut

* Edge band and material libraries are **example data**, not supplier catalogues: enter your real thickness, sheet
  size and grain.
* Cut size = finished size minus edge band thickness. **No pre-milling or trimming allowance** is added.
* Displayed sums may differ from the displayed lines by 0.1 mm (no internal rounding).
* Cutting and nesting are **heuristics**: valid layouts, not necessarily the fewest sheets.

## Appliances, kitchens and wardrobes

* Appliance niches and gaps use **generic sizes**, never matched to a real appliance. Use the installation sheet.
* Worktops and plinths: straight runs only, butt joints only; they do not follow later cabinet edits.
* Sliding doors: one inset two-lane system; no face-mounted tracks, soft-close or door-weight checks.
* Corners: blind corner and 90° L-shape only.
* Frame-and-panel fronts use generic frame defaults; glass-panel fronts cannot be set up in the desktop window.
* Wardrobe rail limits (500 mm depth, 800 mm hanging space) are generic rules of thumb.

## Documents

* Quotes: no rounding to purchasable units, no discounts; unpriced lines are listed, never estimated.
* Packing: weights need a material density; packaging and hardware weight are not included.
* Assembly sheets: generic order, **not a verified assembly instruction**.
* Printing: check the 100 mm calibration mark; the program cannot detect printer scaling.
* Drawings: hidden lines are exact for box-shaped parts only.

## CNC

* **No verified postprocessor, no verified machine.** See the warning in [chapter 10](10-cam-experimental.md).
  Generated code must be reviewed and proven by a qualified operator.

## Not available (yet)

* In the desktop window: desks and tables (command line), merging projects (command line), workshop remnant store
  (command line), CAM (command line), batch quote (command line), glass-panel fronts, plugins.
* No STEP import/export; no exchange with bCAD, Astra, PRO100, GibLab or ViyarPro.
* Proprietary CNC formats (Homag, Biesse, SCM and others).
* The desktop application has been built and tested mainly on Linux; the Windows build is a preview.

## Your responsibility

FurnitureOS helps with design and paperwork. The furniture maker remains responsible for checking dimensions,
hardware, appliance fit, load capacity, wall fixing and machine safety before production.
