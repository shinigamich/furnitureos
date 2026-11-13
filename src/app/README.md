<!-- SPDX-License-Identifier: AGPL-3.0-only -->
# `app` — the FurnitureOS desktop application

Module `app` (Qt 6 Widgets). Two targets:

| Target | Contents | Qt |
|---|---|---|
| `fos_app_core` (`fos::app_core`) | Widget-free view-model logic, fully unit-tested: `Session`, browser tree, inspector parameter rows and input parsing, interior / front / hardware / part-override editors, drawing annotation editing (picking, commands, edit list), diagnostics rows, cutting summary and settings, drawing requests and exports, translated captions for the production module, recent files, autosave decisions, window title, command label text | Core, Gui (no widgets) |
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
| `cutting_rows.hpp` | `cuttingSummary` (numbers from the heuristic optimiser), `cuttingSettingsCommand` (kerf / trim, or a `CuttingSettingsInput` with kerf, trim, objective and remnant list), `cuttingSettingsInput`, `hasOwnCuttingSettings`, `useDefaultCuttingSettingsCommand`, `nextRemnantId` ("R1", "R2", …) |
| `interior_rows.hpp` | `sectionRows` (section tree depth-first), `sectionCaption` / `sizeDescription` / `contentText`, size syntax `parseSectionSize` / `sizeText` / `parseSizeList` (`300` = fixed mm, `1*` = share, anything else = expression), `splitCommand` (equal shares), `removeSplitCommand` (split section → empty leaf, front kept), `sectionSizeCommand`, `clearInteriorCommand` (`setInterior` with an empty leaf), `FrontInput` + `frontInput` / `frontCommand` (none / door / door pair / drawer stack) |
| `cabinet_parts.hpp` | `HardwareRole` (hinge, handle, shelf pin, confirmat, dowel, foot), `hardwareChoices` (library entries of the matching `HardwareKind`), `hardwareCommand`; `partSlot`, `currentOverride`, `edgeChoice`, `partMaterialCommand`, `partEdgeCommand` (cabinet rule / raw / band), `clearPartOverrideCommand` |
| `drawing_edits.hpp` | `drawingIdOf`, `editContext`, `findDimension`, picking: `viewAt` (sheet view placements), `dimensionAt` (distance to the primitives carrying the dimension's `elementId`, 1.5 paper mm), `elementBounds`, `nearestCorner` (every part corner through `drawings::resolveAnchor`); commands: `dimensionOffsetCommand`, `hideDimensionCommand`, `dimensionTextCommand` (records `measuredAtEdit` via `drawings::setTextOverride`), `removeEditCommand`, `addUserDimensionCommand` (two `ModelAnchor`s, id `user.dim.<n>`); `drawingEditRows` / `editRowText` (stale and orphan flags from the `EditReport`), `editFindings` |
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
* **Inspector.** Cabinet: name, then three tabs (the selected tab and interior section survive rebuilds):
  * *Parameters*: grouped by kind with units (number or expression, Enter commits; derived parameters are
    marked, a pinned one has Reset).
  * *Construction*: construction options; materials and edge bands by role; **hardware** (hinge, handle, shelf
    pin, confirmat screw, dowel, foot — each a combo of the project library entries of that kind,
    `commands::setHardware`).
  * *Interior*: the section tree (columns section, size, content) with **Split vertically / Split
    horizontally** into 2–9 equal shares (`splitSection`), **Remove split** (`replaceSection` with an empty leaf,
    front kept), **Empty interior** (`setInterior`), **Set size** of a child section (`setSectionSize`; `300` =
    fixed mm, `1*` / `2*` = share of the rest, other text = expression), **Shelves** of a leaf
    (`setShelfCount`) and the **front** of the selected section: none, single door (hinge side), door pair,
    drawer stack (heights bottom to top, `;`-separated, same size syntax), overlay, cup hinges, handle
    position (`setFront`).
  A refused edit shows the engine's message (under the field or under the action buttons) and changes nothing.
  Panel item: name + parameters. Part: read-only finished size, cut size (`domain::cutSize`), material, grain,
  edges, operation count; for a cabinet part an **override** editor (material, and each edge L1/L2/W1/W2 as
  cabinet rule / raw / an edge band of the library; Remove override) through `commands::setPartOverride`.
* **Workspaces.** 3D (`viewport::ViewportWidget`, selection synchronised both ways, View menu presets /
  projection / display mode / fit all); Drawings (assembly drawing of each item and part drawings of the selected
  item; paper A4–A1, orientation, fit or fixed scale 1:1…1:50, sheet, zoom; PDF/SVG/DXF export; print at exact
  scale with fall-back to an explicitly confirmed fit-to-page, followed by the plan's calibration-mark note);
  Production (cut list, parts, materials, edge banding, hardware, machining tables; CSV export with UTF-8 BOM);
  Cutting (per material summary, sheet layouts from `cutting::renderSheetSvg`, labelled as heuristic; kerf,
  trim, **objective** (fewest sheets / least waste / remnants first / fewest cuts) and — per material — the
  **remnant list** (label, length, width, quantity) through `commands::setCuttingSettings`; "Use project
  defaults for this material" removes the material's own entry).
* **Drawing annotations** (panel to the right of the sheet preview). A click on the preview selects the nearest
  visible dimension (hit test against the primitives with its `elementId`, highlighted in blue). For the
  selected dimension: **Offset** (paper mm, signed like `Dimension::offset`), **Text** override (empty = measured
  value; the measured value is recorded so a later model change marks it stale), **Hide**, **Reset** (removes
  the stored edit) — `commands::setElementEdit`. **Pick two corners** adds a horizontal / vertical user
  dimension between the part corners nearest to two clicks in the same view (`ModelAnchor`, follows the model;
  `commands::setDrawingEdits`); a user dimension can be re-offset, re-texted or deleted but not hidden. The
  panel lists every stored edit of the drawing (hidden / offset / text, STALE, ORPHAN) with **Remove edit**,
  and the `drawings.edits.*` findings (stale overrides, orphans, bad anchors) verbatim.
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

* `fos_app_core_tests` (GoogleTest, `QT_QPA_PLATFORM=offscreen`): interior editor (size syntax; split vertically
  → partition 684 mm and shelf 271 × 540 mm by hand, shelf count, fixed / share / expression sizes 198 / 362 /
  148 mm, horizontal split, remove split, empty interior, single door 716 × 596, drawer stack 140 / 285 / 285,
  no front; nine refused edits leave interior, undo label and revision unchanged; undo / redo), hardware (library
  choices, selection changes the generated instances, missing ids reported, refused edit), part overrides
  (material, raw / banded edges, empty override removed, refusal, undo), drawing edits (hit test, view at a
  point, offset / text / hide / remove with undo / redo, stale override after a width change, user dimension
  between corners 820 → 900 mm following the model, refused anchors, orphans and their removal), cutting
  settings (objective, remnant as extra stock, refused remnants / kerf, back to defaults, undo / redo);
  title, recent files, autosave decisions and
  recovery offers, command labels, every template, Scenario B at the view-model level (width 600 → 700 typed
  into the inspector model: shelf 562 → 662 mm, bottom 664 mm, cut list follows; undo / redo; clean at the save
  point), expressions and refused inputs leave the project unchanged, save / open / broken file, autosave +
  recovery + undo back to disk state, add / duplicate / delete items, selection normalisation, parameter grouping,
  browser tree, diagnostics rows, cutting summary + kerf command, drawings (A3 sheet, explicit scale too large is
  refused, SVG / DXF / PDF export), English captions without a translator.
* `fos_app_gui_tests` (CTest `app_gui_smoke`, label `gui`; run under `xvfb-run`, exit 77 = skipped without a
  display): base cabinet from template, width 900 typed into the inspector (shelf 862 mm), refused input shows an
  inline error, browser lists 10 parts, drawings preview is non-empty, production / cutting views filled, save,
  reopen, width 900. Through the widgets: split the interior vertically, 2 shelves in the left section, door
  pair → 12 parts with sizes checked by hand (partition 684, shelves 271 × 540, doors 716 × 296.5), a refused
  size shows the inline error, undo restores the 10 template parts; hardware combo, part material override,
  a click on the width dimension + text override (recorded at 600 mm, stale at 700 mm), a user dimension by
  two corner clicks (820 mm), cutting objective + remnant. With `FOS_APP_SHOT_DIR=<dir>` it writes 1440 × 900
  screenshots of the four workspaces and of the new editors (part override, interior, front, hardware, drawing
  annotations, cutting remnants) in English and Ukrainian.

## Limitations

* Core diagnostics, part names generated by the furniture module ("Left side") and library names are English
  data, not UI strings; they are not translated.
* Not editable in the UI yet (although the project commands exist): panel items' material / orientation,
  custom parameters (add / remove), library entries, the remaining cutting options (minimum remnant size,
  stages, first cut), project metadata. Drawing edits other than dimension offset / text / hide and user
  dimensions (text shifts, user texts, leaders, detail markers) are listed and removable but cannot be created
  or changed in the UI. Splits always create equal shares (sizes are then set per section).
* Remnants are edited per material only (with the scope "Project defaults" the remnant table is disabled,
  although the core would accept default remnants; existing ones are kept on Apply).
* Hardware ids that are not in the library and edge bands missing from the library are accepted by the
  furniture module with a generation finding (the editors only offer library entries).
* Picking a corner snaps to the nearest projected corner of any part in the view, which may be a front's corner
  when fronts overlap the carcass; click just outside the corner you mean.
* The drawing edit list shows element ids (e.g. `cab1/view.front/dim.overall.width`); hardware and material
  combos show the library names (English data).
* No item placement editing (move / rotate) other than the automatic placement of added / duplicated cabinets.
* DXF export writes one file per view (R12 has no paper space here); see `src/drawings/include/fos/drawings/dxf.hpp`.
* The Help ▸ About source link is the CMake cache value `FOS_APP_SOURCE_URL`; set it for distributed builds.
* Only Linux (X11 / offscreen) has been built and tested.
