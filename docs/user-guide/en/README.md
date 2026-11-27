<!-- SPDX-License-Identifier: AGPL-3.0-only -->
# FurnitureOS user guide (English)

A practical guide for furniture makers who use the FurnitureOS desktop application. It describes only what the
program can do today. Parts marked **Experimental** or **Command line only** are clearly labelled.

> FurnitureOS is in early development (preview build). Hardware data, appliance sizes and CNC output are
> **generic and unverified**. Read [chapter 13](13-limitations-and-safety.md) before you cut or drill anything.

Українська версія: [../uk/README.md](../uk/README.md)

## Chapters

1. [First steps](01-first-steps.md): installing, the main window, workspaces, the 3D view
2. [Creating furniture from templates](02-templates.md): every template, corner and appliance units, wardrobe, desk
3. [Editing cabinets](03-editing-cabinets.md): parameters, interior, doors, drawers, rails, sliding doors, flaps, frame fronts
4. [Materials, edge bands, hardware and the library editor](04-materials-and-libraries.md): prices and density
5. [Kitchen tools](05-kitchen-tools.md): worktops, plinths, the room plan
6. [Drawings](06-drawings.md): views, dimensions, isometric and exploded views, PDF export, printing
7. [Production documents](07-production.md): part list, cut list, edge banding, labels, hardware, packing, assembly, job pack
8. [Cutting and nesting](08-cutting-and-nesting.md): sheets, remnants, router nesting
9. [Quotes](09-quotes.md)
10. [CAM (experimental)](10-cam-experimental.md): read the safety warning first
11. [Files](11-files.md): saving, autosave and recovery, components, merging, comparing, production batches
12. [Keyboard shortcuts](12-keyboard-shortcuts.md)
13. [Limitations and safety](13-limitations-and-safety.md)
14. [Updates](14-updates.md)

## Conventions

* Menu paths are written **File ▸ Save**. Lengths are always in millimetres.
* "Command line only" means the feature is available in the `fos` command-line tool that ships next to the
  desktop application (`fos.exe` on Windows), but not yet in the desktop window. Run `fos --help` in a terminal.
* Every accepted change can be undone with **Edit ▸ Undo**. An input the program refuses changes nothing; the
  reason is shown in red next to the field or in the status bar.
