# Changelog

All notable changes to FurnitureOS are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); versions follow
[semantic versioning](https://semver.org/). Pre-releases carry a suffix such as `-beta.1`.
The release workflow publishes the section whose heading matches the tag as the release notes
(see `docs/development/releasing.md`).

## [Unreleased]

## [0.1.0] - 2026-11-27

First test release. **This is a preview for trying the program, not a production tool**: nothing has
been used in real production yet, and every output that goes to a workshop or a machine must be checked
by hand. The full list of limits is in `KNOWN_LIMITATIONS.md`.

### Added

- **Desktop application** (Windows, Linux; English and Ukrainian): parametric cabinets, wardrobes,
  kitchen units, desks and tables from templates; editors for interior (shelves, partitions, drawers,
  inner drawers, clothes rails), fronts (doors, drawers, lift-up flaps, sliding doors, frame-and-panel and
  glass fronts), hardware choices, part overrides and part shapes; 3D view with measure, section,
  realistic display and resize handles; top-down plan view; undo/redo; autosave with crash recovery.
- **Furniture types**: carcass cabinets, blind-corner and L-shaped corner cabinets, appliance housings
  (oven niche, dishwasher gap), sliding-door wardrobes, desks and tables (panel ends or generic legs,
  drawer pedestal), drawer boxes with generic slides, kitchen worktops and plinths over straight runs.
- **Shaped parts**: corner radii, arched edges, custom outlines and cut-outs; DXF and SVG outline import.
- **Production documents**: cut lists, edge-banding lists and job lists, hardware and bought-in lists,
  packing lists, generic assembly sheets, part labels with Code 128 barcodes, a printable job pack (HTML),
  cost estimates and quotes (missing prices are listed as unpriced, never invented).
- **Cutting**: guillotine sheet cutting optimiser, true-shape nesting for shaped parts, linear (1D)
  cutting and edge-band roll planning, workshop offcut stock; every layout is checked by an independent
  validator.
- **Drawings**: part, assembly, isometric and exploded views with dimensions; SVG, DXF and PDF export;
  exact-scale printing with a calibration mark.
- **Validation**: interference checks, model checks, optional engineering rule checks (labelled heuristics).
- **CAM foundation**: drilling, contour routing with true arcs and pocket clearing, tool library files,
  a simulator that refuses programs that would cut below the stock or miss a feature.
- **Project files** (`.fosp`) with atomic save and backups; reusable components (`.foscomp`), production
  batches (`.fosbatch`), libraries (`.foslib`); declarative data plugins.
- **Command-line tool `fos`** for everything above (create, edit, validate, cut, drawings, CAM, quotes,
  batches, comparisons).
- **Windows packages**: per-user installer (no administrator rights) and a portable ZIP; Linux tarball.

### Known limitations

- **Hardware data is generic and unverified.** Hinges, slides, connectors, tracks, hangers and legs are
  generic models, not manufacturer data. Check every drilling position against the real fitting's data
  sheet before drilling.
- **CAM output is unverified.** The G-code is a generic dialect that has never been run on a physical
  machine; no postprocessor is verified for any controller. It must be reviewed and proven by a qualified
  operator.
- Cutting and nesting are heuristics, not optimal solvers.
- Exact-scale printing has not been checked on a physical printer by the project.
- Worktops and plinths: straight runs only. Contours: lines and arcs only (no splines or Béziers).
- Generated part names are English only; the Ukrainian interface text still needs native review.
- The Windows installer and executables are **not code-signed**: Windows SmartScreen shows a warning on
  first start (see `docs/user-guide/installing-windows.md`).
- No macOS build.

[Unreleased]: https://github.com/shinigamich/furnitureos/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/shinigamich/furnitureos/releases/tag/v0.1.0
