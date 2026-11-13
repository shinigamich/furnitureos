<!-- SPDX-License-Identifier: AGPL-3.0-only -->
# `app` — the FurnitureOS desktop application

Module `app` (Qt 6 Widgets). Two targets:

| Target | Contents | Qt |
|---|---|---|
| `fos_app_core` (`fos::app_core`) | Widget-free view-model logic, fully unit-tested: `Session`, browser tree, inspector parameter rows and input parsing, diagnostics rows, cutting summary, drawing requests and exports, translated captions for the production module, recent files, autosave decisions, window title, command label text | Core, Gui (no widgets) |
| `furnitureos` (+ static `fos_app_gui`) | Main window, docks, workspaces, dialogs | Widgets, OpenGLWidgets (viewport), PrintSupport, Svg |

The GUI targets are configured only when `fos::viewport` exists (`if(TARGET fos::viewport)`); otherwise only
`fos_app_core` and its tests are built.

**No manufacturing calculation lives here.** Every edit is a `project::Command` executed by
`project::Document`; everything shown is regenerated from `project::resolve()` and the project outputs
(`productionReports`, `cuttingPlans`, `assemblyDrawing` / `partDrawing`) and `validation::validateModel`.
The only numbers the UI derives itself are display layout (zoom, where a new cabinet is placed: to the right of the
existing items' bounding boxes).

## Headers (`include/fos/app/`)

| Header | Contents |
|---|---|
| `session.hpp` | `Session` (QObject): document + file path, `newProject`, `newFromTemplate`, `addFromTemplate`, `open`, `openRecovered`, `save`, `saveAs`, `autosave`, `execute`/`undo`/`redo`, `deleteItem`, `duplicateItem`, `resolved()`, lazily computed `productionReports()`, `cuttingPlans()`, `validationReport()`, `Selection` (item + part ids). Signals `documentReplaced`, `projectChanged`, `selectionChanged`, `savedStateChanged`. |
| `templates.hpp` | `TemplateKind` (6 furniture templates), `templateTitle`, `defaultTemplateSize` (read from the template's graph), `makeTemplateItem` (W/H/D set in one atomic furniture edit), `nextItemId`, `uniqueItemName` |
| `parameter_rows.hpp` | `parameterGroups` (by `ParameterKind`; standard inputs, then derived, then custom), `kindTitle`, `unitSuffix`, `editText`, `valueText`, `parseParameterInput` (plain decimal → value, else expression), `parameterCommand`, `applyParameterInput` |
| `browser_tree.hpp` | `buildBrowserTree`: item → assembly → parts |
| `diagnostics_rows.hpp` | generation + validation findings with part / item links, `summarize` |
| `cutting_rows.hpp` | `cuttingSummary` (numbers from the heuristic optimiser), `cuttingSettingsCommand` (kerf / trim) |
| `drawing_request.hpp` | `DrawingRequest`, `generateDrawing` (translated title block + calibration note), `exportDrawingSvg/Dxf/Pdf` |
| `production_text.hpp` | `ProductionText::localisation()/htmlSheetText()/labelText()` — every production caption via `tr()` |
| `recent_files.hpp`, `autosave.hpp`, `window_title.hpp`, `command_text.hpp`, `version.hpp` | MRU list in QSettings; `autosaveDue`, `recoveryOffer`, untitled autosave location; "name* — FurnitureOS"; translated undo labels with item names |

## Behaviour

* **Lifecycle.** New / New from template (size dialog) / Open / Open recent / Save / Save As of `.fosp`;
  the dirty flag of `Document` drives the `*` in the title; closing or replacing a modified project asks
  Save / Discard / Cancel. Errors show the core's message verbatim.
* **Autosave** every N minutes (Preferences, default 5, 0 = off) through `Document::autosave` to
  `<file>.autosave`; untitled projects autosave to `<AppDataLocation>/autosave/untitled-<project id>.fosp.autosave`.
  An autosave is written only when the project is modified and the revision changed since the last one.
* **Recovery.** Opening a file whose newest autosave/backup (`formats::listRecoveryCandidates`) is newer than
  the file offers to recover it (`project::recoverProject`); the recovered state is applied as one undoable
  step on top of the saved file, so Undo returns to what is on disk. At start-up an untitled autosave is offered.
* **Inspector.** Cabinet: name, parameters grouped by kind with units (number or expression, Enter commits;
  derived parameters are marked, a pinned one has Reset), construction options, materials and edge bands by
  role from the project library. A refused edit shows the engine's message under the field and changes nothing.
  Panel item: name + parameters. Part: read-only finished size, cut size (`domain::cutSize`), material, grain,
  edges, operation count.
* **Workspaces.** 3D (`viewport::ViewportWidget`, selection synchronised both ways, View menu presets /
  projection / display mode / fit all); Drawings (assembly drawing of each item and part drawings of the selected
  item; paper A4–A1, orientation, fit or fixed scale 1:1…1:50, sheet, zoom; PDF/SVG/DXF export; print at exact
  scale with fall-back to an explicitly confirmed fit-to-page, followed by the plan's calibration-mark note);
  Production (cut list, parts, materials, edge banding, hardware, machining tables; CSV export with UTF-8 BOM);
  Cutting (per material summary, sheet layouts from `cutting::renderSheetSvg`, labelled as heuristic; kerf and
  trim per material or project default through `commands::setCuttingSettings`).
* **Exports (File ▸ Export).** Cut list CSV, production sheet HTML (selected or first item), part labels SVG
  (A4, 70 × 37 mm, one file per page), drawing PDF / SVG (one file per sheet) / DXF (one file per view, model
  space 1:1 mm), model OBJ + MTL.

## Localisation

All UI strings use `tr()` / `QCoreApplication::translate`. Catalogues: `translations/furnitureos_uk.ts`
(complete Ukrainian translation) and `furnitureos_en.ts` (source language). Regenerate after changing strings:

```bash
cmake --build build/<dir> --target furnitureos_lupdate   # or update_translations
```

The `.qm` files are compiled at build time and embedded under `:/i18n/`. The language is chosen in
Edit ▸ Preferences (System default / English / Українська) and applied at the next start; "System default"
means Ukrainian for a Ukrainian system locale, English otherwise.

## Tests

* `fos_app_core_tests` (GoogleTest, `QT_QPA_PLATFORM=offscreen`): title, recent files, autosave decisions and
  recovery offers, command labels, every template, Scenario B at the view-model level (width 600 → 700 typed
  into the inspector model: shelf 562 → 662 mm, bottom 664 mm, cut list follows; undo / redo; clean at the save
  point), expressions and refused inputs leave the project unchanged, save / open / broken file, autosave +
  recovery + undo back to disk state, add / duplicate / delete items, selection normalisation, parameter grouping,
  browser tree, diagnostics rows, cutting summary + kerf command, drawings (A3 sheet, explicit scale too large is
  refused, SVG / DXF / PDF export), English captions without a translator.
* `fos_app_gui_tests` (CTest `app_gui_smoke`, label `gui`; run under `xvfb-run`, exit 77 = skipped without a
  display): base cabinet from template, width 900 typed into the inspector (shelf 862 mm), refused input shows an
  inline error, browser lists 10 parts, drawings preview is non-empty, production / cutting views filled, save,
  reopen, width 900. With `FOS_APP_SHOT_DIR=<dir>` it writes 1440 × 900 screenshots of the four workspaces in
  English and Ukrainian.

## Limitations

* Core diagnostics, part names generated by the furniture module ("Left side") and library names are English
  data, not UI strings; they are not translated.
* The inspector edits parameters, construction options and role materials. Not editable in the UI yet (although
  the project commands exist): interior sections (splits, shelf counts, fronts), hardware selection, per-part
  overrides, panel items' material/orientation, drawing edits (moving dimensions, texts), library entries,
  remnants and the other cutting options (objective, stages, first cut), project metadata.
* No item placement editing (move / rotate) other than the automatic placement of added / duplicated cabinets.
* DXF export writes one file per view (R12 has no paper space here); see `src/drawings/include/fos/drawings/dxf.hpp`.
* The Help ▸ About source link is the CMake cache value `FOS_APP_SOURCE_URL`; set it for distributed builds.
* Only Linux (X11 / offscreen) has been built and tested.
