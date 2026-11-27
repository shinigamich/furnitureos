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
| `nesting_rows.hpp` | `NestingSummaryRow` / `nestingSummary` (numbers of the heuristic nesting plans; `problems` = Error findings of `validateNest`), `PlanKind`, `CuttingTableRow` / `cuttingTableRows`, `nestingSettingsInput`, `nestingSettingsCommand` (`setNestingSettings`), `parseNestingSeed` |
| `cutting_rows.hpp` | `cuttingSummary` (numbers from the heuristic optimiser), `cuttingSettingsCommand` (kerf / trim, or a `CuttingSettingsInput` with kerf, trim, objective and remnant list), `cuttingSettingsInput`, `hasOwnCuttingSettings`, `useDefaultCuttingSettingsCommand`, `nextRemnantId` ("R1", "R2", …) |
| `interior_rows.hpp` | `sectionRows` (section tree depth-first), `sectionCaption` / `sizeDescription` / `contentText`, size syntax `parseSectionSize` / `sizeText` / `parseSizeList` (`300` = fixed mm, `1*` = share, anything else = expression), `splitCommand` (equal shares), `removeSplitCommand` (split section → empty leaf, front kept), `sectionSizeCommand`, `clearInteriorCommand` (`setInterior` with an empty leaf), `FrontInput` + `frontInput` / `frontSpec` / `frontCommand` (none / door / door pair / drawer stack; drawer stack with optional drawer box: `drawerBox`, `slide`, `drawerBottom` → `FrontSpec::drawerBox`), `slideChoices` (library entries of kind DrawerSlide) |
| `cabinet_parts.hpp` | `HardwareRole` (hinge, handle, shelf pin, confirmat, dowel, foot, sliding system, cabinet hanger (optional, "None" = empty), wall rail (kind Other with `rail_height` / `rail_thickness`)), `hardwareRoleOptional`, `hardwareChoices` (library entries of the matching `HardwareKind`), `hangerHint` (grooved back with groove distance ≥ the hanger's back space), `hardwareCommand`; `partSlot`, `currentOverride`, `edgeChoice`, `partMaterialCommand`, `partEdgeCommand` (cabinet rule / raw / band), `clearPartOverrideCommand` |
| `contour_rows.hpp` | Shape editor view-model: `VertexRow`, `CutoutRow`, `ContourInput`, `contourInput(spec)`, `contourSpec(input)` (only the selected kind's fields + cut-outs; Rectangle without cut-outs = no shape; plain numbers normalised, other texts passed on as expressions; only a non-numeric bulge is refused here), `rectangleVertexRows`, `moveRow`, `ContourTarget` / `contourTarget` (cabinet slot or panel item), `currentContour`, `contourCommand` (`setPartContour` / `setPanelContour`), `applyContourInput`, `ShapeFacts` / `shapeFacts` (shaped, routed = `!isRectangularOutline`, outline area mm² via `geometry::signedArea` minus cut-outs, segments, cut-outs, banded segments), `shapeSummary` |
| `drawing_edits.hpp` | `drawingIdOf`, `editContext`, `findDimension`, picking: `viewAt` (sheet view placements), `dimensionAt` (distance to the primitives carrying the dimension's `elementId`, 1.5 paper mm), `elementBounds`, `nearestCorner` (every part corner through `drawings::resolveAnchor`); commands: `dimensionOffsetCommand`, `hideDimensionCommand`, `dimensionTextCommand` (records `measuredAtEdit` via `drawings::setTextOverride`), `removeEditCommand`, `addUserDimensionCommand` (two `ModelAnchor`s, id `user.dim.<n>`); `drawingEditRows` / `editRowText` (stale and orphan flags from the `EditReport`), `editFindings` |
| `drawing_request.hpp` | `DrawingRequest`, `generateDrawing` (translated title block + calibration note), `exportDrawingSvg/Dxf/Pdf` |
| `production_text.hpp` | `ProductionText::localisation()/htmlSheetText()/labelText()` — every production caption via `tr()` |
| `resize.hpp` | Direct modelling: `ResizeAxis`, `ResizeHandle`, `ResizeTarget` / `resizeTarget(session, itemId)` (cabinet: nominal envelope from width/depth/height; panel: box of its part), `resizeParameter` (panel axes by orientation), `resizeCommand` (ONE combined command, see Behaviour), `applyResize`, `resizeLabel` ("W", "H", "D", "L") |
| `component_browser.hpp` | Component library view-model: `defaultComponentFolder` (AppDataLocation`/components`, created on first use), `ComponentBrowserModel` (`rescan(folders)` → scan diagnostics, `rows(tag, search)` = categories with matching entries, uncategorised last), `ComponentDetails` / `componentDetails` (name, description, author, tags, "W × H × D mm" preview size, embedded materials / edge bands / hardware as "Name (id)"), `componentConflicts` (entries `planComponentInsert(RenameWithNewId)` would rename = same id, different content), `insertComponentIntoSession` (`insertComponent`, id `cab<n>` / `panel<n>`, placed at `rightEdge()`, selected, one undo step), `componentFileName`, `saveItemAsComponent` (`makeComponent` + `saveComponents` into `<folder>/<id>.foscomp`; `AlreadyExists` unless overwrite), `parseTagList` |
| `batch_editor.hpp` | Production batch window view-model over `fos/project/batch.hpp`: `batchEntryPath` (relative to the batch file like `fos batch add`, absolute while untitled), `BatchEditor` (new / open / save / `saveAs` rebasing relative paths, `addProject` (re-adding adds to the quantity), `removeEntry`, `setQuantity` / `setKey` / `setNote`, `setMaterialSubstitutions`, `setCutListGroupAcrossAssemblies`; every edit checked by `checkBatch`; `settingsProject` + `executeSettingsCommand` run the project cutting / nesting settings commands on the batch settings; `evaluate` = `loadBatchProjects` + `mergeBatch` + `batchSummary`), `BatchEvaluation` (`incomplete` = merge errors), `batchDiagnosticRows` (with project key), `batchMaterialChoices`, `exportBatchCutListCsv` / `exportBatchLabels` (`labels-<n>.svg`) / `exportBatchCuttingPlans` (`<material>-sheet-<n>.svg`, `<material>-nest-<n>.svg`) |
| `library_editor.hpp` | Library editor (Project ▸ Libraries…): `libraryRows` (with `usedBy` items), `validateMaterial/EdgeBand/Hardware` (translated problems), `newMaterial`/`newEdgeBand`, `duplicate*` (`<id>-copy`), `HardwareInput` + `applyHardwareInput` (kind, params, `dimensionsVerified` kept), `applyMaterial/EdgeBand/Hardware` (validate + `commands::upsert*`, one undo step), `removeLibraryEntry` (`commands::remove*`, refused while in use), `exportProjectLibrary` / `previewLibraryImport` / `importLibraryCommand` (`.foslib` via formats/library.hpp, one undo step, renamed entries `<id>-imported`), `LibraryText` |
| `plan_view.hpp` | Plan workspace geometry (project XY, mm, seen from above): `PlanFootprint` / `planFootprints` (flat parts contribute their true outline, contours tessellated at 0.5 mm; other parts their XY box; union = footprint; `bounds` = XY of the assembly bounds), `planExtents`, `PlanRoom` + `checkPlanRoom` (session-only rectangle: the project file has no room/plan settings), `PlanGap` / `planGapsX` (nearest right neighbour sharing Y range, plus room walls; touching gaps <= 0.05 mm omitted), `PlanSnap` / `snapPlanMove` (min/max edges to other items' edges and room walls within a tolerance in mm = snap px / zoom; unsnapped axes rounded to a 1 mm grid), `movedPlacement`, `rotatedPlacement` (exact +90° about Z), `planMoveCommand` / `planRotateCommand` / `applyPlanMove` / `applyPlanRotate` (one `commands::setPlacement` = one undo step), `planItemAt`, `planSizeText`, `planLengthText` |
| `edit_tools.hpp` | Free modelling on whole items (project frame, mm; boxes = generated assembly AABBs): `groupCommands` (several commands = ONE undo step, all or nothing), `itemBounds` / `itemsBounds`, `rotationAbout` (exact quarter turns), `translateItemsCommand`, `moveItemsToCommand` (selection min corner to a point), `rotateItemsCommand` (about the box centre; X/Y for free panels only), `mirrorItemsCommand` (copies whose box is the mirror image; rectangular aligned panels exactly reflected with L1/L2 or W1/W2 bands swapped, others listed in `CopyPlan::notReflected`), `arrayItemsCommand`, `duplicateItemsCommand`, `removeItemsCommand`, `alignItemsCommand` (to the first item), `distributeItemsCommand` (equal gaps, outer items fixed); session helpers `deleteSelectedItems`, `duplicateSelectedItems`, `nudgeSelectedItems`. `Selection::extraItemIds` / `allItems()` + `Session::selectItems` / `toggleItemSelected` hold the multi-selection. |
| `panel_tools.hpp` | Edit > Add panel…: `AddPanelRequest` (size, orientation, material, edge bands, `PanelPlacement`: origin, at / on top of / below / left / right / in front of / behind the selection, `FillGap` between two items), `planAddPanel` (gap = clearance along the separating axis × overlap along the others; thickness from the material, must fit), `makePanelItem`, `applyAddPanel` (one undo step). |
| `item_clipboard.hpp` | Copy / cut / paste of items as clipboard JSON (`{"format":"furnitureos-items","version":1,"project":…}`), pasted with `project::commands::mergeItems` (library entries merged, `RenameWithNewId` on conflicts, clashing item ids renamed); `pasteOffset` pastes in place unless the boxes overlap target items (then right of all items). |
| `update_version.hpp`, `update_release.hpp`, `update_zip.hpp`, `update_plan.hpp` | In-app updater logic (see "Updates"): `SemVer` / `parseSemVer` / `compareSemVer` (semver 2.0 precedence incl. pre-releases); `releasesApiUrl`, `parseGitHubReleases` (object or array), `selectUpdate`, `parseUpdateManifest` (update.json cross-checked against the release), `parseSha256Sums`, `checkChecksumsAgree`, `verifyDownload` / `verifyDownloadedFile`; `crc32`, `inflateRaw`, `extractZip` (stored + deflate, CRC-checked, no path traversal), `safeZipEntryPath`; `detectInstallKind`, `chooseUpdate`, `planPortableSwap`, `locateStagedRoot`, `portableSwapScript` / `installerScript` (.cmd text), `parseUpdateOutcome`, `UpdateConsent` |
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
  * *Construction*: construction options; materials and edge bands by role (including **Drawer boxes** /
    **Drawer bottoms** = `MaterialAssignment::drawer` / `drawerBottom`, empty = default); **hardware** (hinge, handle, shelf
    pin, confirmat screw, dowel, foot — each a combo of the project library entries of that kind,
    `commands::setHardware`).
  * *Interior*: the section tree (columns section, size, content) with **Split vertically / Split
    horizontally** into 2–9 equal shares (`splitSection`), **Remove split** (`replaceSection` with an empty leaf,
    front kept), **Empty interior** (`setInterior`), **Set size** of a child section (`setSectionSize`; `300` =
    fixed mm, `1*` / `2*` = share of the rest, other text = expression), **Shelves** of a leaf
    (`setShelfCount`), a **Clothes rail** of a leaf (checkbox, height from top, rail / support library entries →
    `hangingRailCommand` = `replaceSection` with `Section::hangingRail`; engine refusals shown; tree text
    "clothes rail") and the **front** of the selected section: none, single door (hinge side), door pair,
    drawer stack (heights bottom to top, `;`-separated, same size syntax), overlay, cup hinges, handle
    position (`setFront`). For a drawer stack, **Drawer boxes** none / box on slides, **Slides** (library
    entries of kind DrawerSlide: `hw.slide_ball_bearing`, `hw.slide_undermount`) and **Drawer bottom**
    (grooved / nailed under) → `FrontSpec::drawerBox`; the furniture module sizes the boxes and refuses
    impossible combinations (message shown inline).
  A refused edit shows the engine's message (under the field or under the action buttons) and changes nothing.
  Panel item: name + parameters. Part: read-only finished size, cut size (`domain::cutSize`), material, grain,
  edges, operation count; for a cabinet part an **override** editor (material, and each edge L1/L2/W1/W2 as
  cabinet rule / raw / an edge band of the library; Remove override, which keeps a contour) through
  `commands::setPartOverride`. Shaped parts show "shaped part (routed)" (or "rectangular outline, sawn"), outline
  area, segment / cut-out / banded-segment counts and the blank cut size instead of the four edges.
* **Shape** section (`ShapeEditor`; cabinet part slots and panel items): outline kind Rectangle / Rounded corners
  (4 radius expressions) / Arc edge (edge, rise, convex / concave) / Custom (vertex table x, y, bulge, band-from
  side; add / remove / move up / down; starts from the full rectangle), cut-out table (rectangle x / y / length /
  width / corner radius, circle x / y / radius; add / remove). **Apply** = `setPartContour` / `setPanelContour`
  (undoable); a refused spec shows the core's message inline and changes nothing; **Reset** reloads the stored
  spec; **Remove shape** clears it.
* **Workspaces.** 3D (`viewport::ViewportWidget`, selection synchronised both ways, View menu presets /
  projection / display mode / fit all); Drawings (assembly drawing of each item and part drawings of the selected
  item; paper A4–A1, orientation, fit or fixed scale 1:1…1:50, sheet, zoom; PDF/SVG/DXF export; print at exact
  scale with fall-back to an explicitly confirmed fit-to-page, followed by the plan's calibration-mark note);
  Production (cut list, parts, materials, edge banding, hardware, machining tables; CSV export with UTF-8 BOM);
  Cutting (per material summary, sheet layouts from `cutting::renderSheetSvg`, labelled as heuristic; kerf,
  trim, **objective** (fewest sheets / least waste / remnants first / fewest cuts) and — per material — the
  **remnant list** (label, length, width, quantity) through `commands::setCuttingSettings`; "Use project
  defaults for this material" removes the material's own entry). The summary has one row per material **and
  method** (`cuttingTableRows`: `CuttingPlans::materials` order, saw plan first): "Saw (guillotine)" rows from
  `CuttingPlans::plans`, "Router nesting (heuristic)" rows from `CuttingPlans::nestingPlans` (part instances, sheet
  count, utilisation = `NestResult::utilisation`, unplaced, layout check = `validateNest` "valid" / "problems: N";
  no lower bound). The sheet selector lists every sheet of the selected material ("Saw sheet i of n", then "Nest
  sheet i of n"); nest sheets are drawn from `nesting::renderNestSvg(job, sheetLayout)` in the same `SvgView`
  (sheet size, parts, utilisation, used length below the selector). Parts with a non-rectangular outline are also
  listed as **Routed parts** (name, material, blank size). **Router nesting settings (all materials)**: tool
  diameter, clearance, chord tolerance, four margins, iterations and seed (0 … 4294967295, typed) applied with
  "Apply and re-nest" as `commands::setNestingSettings` (undoable; spin box ranges are wider than the accepted
  ones so out-of-range values reach the command and its message is shown inline; minimum remnant length kept).
  Production tables show the shaped columns of the
  production module (Shape, Outline area, Outline edges, Routed contours, Routing length; translated).
* **Drawing annotations** (panel to the right of the sheet preview). A click on the preview selects the nearest
  visible dimension (hit test against the primitives with its `elementId`, highlighted in blue). For the
  selected dimension: **Offset** (paper mm, signed like `Dimension::offset`), **Text** override (empty = measured
  value; the measured value is recorded so a later model change marks it stale), **Hide**, **Reset** (removes
  the stored edit) — `commands::setElementEdit`. **Pick two corners** adds a horizontal / vertical user
  dimension between the part corners nearest to two clicks in the same view (`ModelAnchor`, follows the model;
  `commands::setDrawingEdits`); a user dimension can be re-offset, re-texted or deleted but not hidden. The
  panel lists every stored edit of the drawing (hidden / offset / text, STALE, ORPHAN) with **Remove edit**,
  and the `drawings.edits.*` findings (stale overrides, orphans, bad anchors) verbatim.
* **Import DXF outline (File menu).** File dialog → options dialog (name, material or "first material of the
  thickness", thickness, layer, units, skip unsupported cut-outs) → `formats::importDxfFile` +
  `project::buildDxfPanelItem` (`fos/project/dxf_panel.hpp`, shared with `fos import-dxf`) → `commands::addItem` (undoable; placed right of the existing items, selected). A refusal
  (unreadable DXF, no closed outline, unsupported cut-out, no material, panel refused) is shown in the dialog
  with the DXF diagnostics and the dialog stays open; diagnostics of a successful import are shown in a message box.
* **Exports (File ▸ Export).** Cut list CSV, production sheet HTML (selected or first item), part labels SVG
  (A4, 70 × 37 mm, one file per page; barcode None / Code 128 of the part id or project|part id, remembered in
  QSettings `export/labelBarcode*`; omitted-barcode warnings such as too narrow labels are shown, also for batch labels), drawing PDF / SVG (one file per sheet) / DXF (one file per view, model
  space 1:1 mm), model OBJ + MTL.
* **Measure** (View menu, toolbar, key M; checkable): puts the 3D view in measure mode
  (`ViewportWidget::setMeasureMode`). The status bar shows the hovered snap kind (corner, outline vertex, edge
  midpoint, face centre, surface point), the live distance while picking the second point, and finally
  "Distance … mm (ΔX …, ΔY …, ΔZ … mm)" (`measureStatusText`, `src/gui/view_tools.cpp`). Esc clears the points,
  a second Esc leaves the mode (the action unchecks). Display only: the project is never modified.
* **Resize handles** (3D view): when an item (cabinet or panel) is selected, `MainWindow::updateResizeHandles`
  passes `resizeTarget()` to `ViewportWidget::setResizeTarget` — handles on the +X/−X (width), +Y/−Y (depth)
  and +Z (height) faces; panels get the faces of their length/width axes, never the thickness, never −Z. On
  `resizeRequested` the window calls `applyResize`: +X/+Y/+Z = `commands::setParameter`; −X/−Y = a combined
  command that runs `setParameter` and then `setPlacement` (placement · translation(−Δ along the assembly
  axis), so the opposite face stays put) on the same project copy — ONE undo step with the parameter's label.
  Document merging was not used (the two commands have different merge keys; merging is for repeated edits of
  one value). A refused value shows "Resize refused: <core message>" in the status bar; the model is unchanged.
  Snap 1 mm, Shift 10 mm; Esc cancels a drag; hidden in measure mode.
* **Components dock** (tabbed with the project browser; `src/gui/component_browser.cpp`): scans the library
  folders of Edit ▸ Preferences ▸ Component folders (empty list = the per-user folder
  `QStandardPaths::AppDataLocation/components`, created on first use). Tree by category, tag combo ("All tags"),
  search over name / id / description / category / tags; the details pane shows description, preview size
  (generated bounding box: depth includes the fronts), category, tags, author, embedded entries and the file.
  The status line counts components and scan problems (messages in its tooltip). **Insert** (button, double
  click, context menu) runs `componentConflicts`; when it is non-empty a dialog lists the conflicting ids and
  asks Keep existing (Skip) / Replace / Add under new ids (RenameWithNewId, ids `<item>.lib1`, …); then
  `insertComponentIntoSession` — undo label "Insert component <name> as <id>". **Save as component…** (Edit menu,
  browser context menu; needs a selected item): name, category (existing ones offered), comma-separated tags,
  description, author, library folder (configured ones or "Other folder…"); an existing file is replaced only
  after confirmation; the browser rescans (an "other" folder is shown until the next Preferences change).
  The application does not load plugins yet, so plugin-contributed components (`LoadReport::components`) are not
  listed.
* **Section plane** (View menu, toolbar, key S; checkable): shows the `SectionToolBar` (axis X/Y/Z, slider +
  spin box in mm over the model bounds along the axis — re-read on every model change — "Flip" and "Off")
  and applies `ViewportWidget::setSectionPlane`; unchecking or "Off" removes the plane. Display only.

## Updates

Optional in-app updates (`src/gui/update_controller.*`, `update_dialog.*`; logic in `update_*.hpp`).
Rules: FurnitureOS works fully offline; checks are opt-in; nothing is installed without the user's explicit
confirmation; no telemetry (one anonymous GET with `User-Agent: FurnitureOS-Updater`, no cookies, no ids).

* **Help ▸ Check for updates…** always checks and reports "up to date" and errors in a dialog.
* **Startup** (`MainWindow::startupUpdateCheck`, called from `main()` only, so tests never hit the network):
  first reports the result file of a previous update attempt; then, if `updates/startupCheck` is `ask`,
  asks once (Yes → `yes`, No → `no`); checks only when `yes`. Failures of an automatic check only go to the
  status bar. A version stored in `updates/skippedVersion` is not offered automatically.
* **Channel** (`updates/includePrereleases`): stable uses `GET https://api.github.com/repos/shinigamich/furnitureos/releases/latest`
  (GitHub excludes drafts and pre-releases); pre-releases use `/releases?per_page=30` and pick the highest
  semantic version. A release is offered only if it is newer than `applicationVersion()` (FOS_VERSION_STRING,
  incl. suffixes such as `-beta.1`) and carries `update.json` and `SHA256SUMS.txt`.
* **Verification**: update.json must match the tag version and the GitHub asset sizes; update.json and
  SHA256SUMS.txt must agree on the hash before downloading; the download (to
  `CacheLocation/updates/<name>.part`, cancellable, aborted when larger than declared) must match size and
  SHA-256; otherwise it is deleted and the update refused.
* **Apply** (Windows; decided by `detectInstallKind` + `chooseUpdate`):
  * `installed.txt` next to the exe (written by the Inno Setup installer) and a `windows-x64-setup` asset →
    script waits for the app to exit, runs `setup.exe /VERYSILENT /SUPPRESSMSGBOXES /NORESTART`, relaunches the exe.
    Installed copy without a setup asset → release page only (never swap files under an installer).
  * Portable → `extractZip` into `<parent>/<app>.update-X.Y.Z` (same volume), `locateStagedRoot` (exe at the
    top or in the single top-level folder), then the script: wait ≤ 120 s for the PID, rename app →
    `<app>.old` (retry 30 s), rename staged → app, roll back on failure, relaunch; the old folder is deleted
    only after success. Every step is a directory rename on one volume, so the install is never half-updated.
  * Linux/other → "Open release page" (no in-place replacement; the dialog says so).
  * The script writes `ok X.Y.Z` / `failed <reason>` to `CacheLocation/updates/update-result.txt`, shown at
    the next start.
* **Why a generated `.cmd` and not a helper executable**: Windows cannot rename a folder while an image in it
  runs, so a helper would have to be copied out together with its Qt and C++ runtime DLLs; `cmd.exe` is always
  present, needs no PowerShell execution policy, and the script text is unit-tested (`update_tests.cpp`).
  It is started detached with `CREATE_NO_WINDOW`, working directory = the cache folder.

**Release contract** (implemented by `packaging/release/make_release_metadata.py` and the release workflow):
GitHub release tagged `vX.Y.Z[-pre]` in `shinigamich/furnitureos` with assets `FurnitureOS-<ver>-windows-x64.zip`
(one top-level folder holding `furnitureos.exe`; stored/deflate entries, no ZIP64, < 4 GiB),
optional `FurnitureOS-<ver>-windows-x64-setup.exe`, `SHA256SUMS.txt` (`sha256sum` format, covering both) and
`update.json` = `{"version", "published", "notes" (Markdown), "assets": {"windows-x64-zip": {name, sha256,
size}, "windows-x64-setup": {...}}}`. Pre-releases must be marked as such on GitHub.

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

* `fos_app_core_tests` (GoogleTest, `QT_QPA_PLATFORM=offscreen`): component browser (`component_tests.cpp`: base
  cabinet saved as `comp.base-600.foscomp` into a temp folder, second save refused with AlreadyExists, scan with a
  missing folder → one `missing_path` warning, tag + search filter, details, preview width / height = template
  size, inserted into a new project as `cab1` with the original part count, second copy at x = right edge, two
  undos → empty; a changed material with the same id is one conflict and RenameWithNewId adds `cab1.lib1`;
  broken file → `load_failed`; file-name / tag helpers; unknown item → NotFound); resize handles (`resize_tests.cpp`: target of
  a 800 × 820 × 560 cabinet, +X 800 → 900 and one undo, −X 800 → 900 moves the origin −100 mm and one undo restores
  width and placement, −Y of a 90°-rotated cabinet moves along its own axis, refused value / −Z / unknown item
  leave the project unchanged, panel axes by orientation); interior editor (size syntax; split vertically
  → partition 684 mm and shelf 271 × 540 mm by hand, shelf count, fixed / share / expression sizes 198 / 362 /
  148 mm, horizontal split, remove split, empty interior, single door 716 × 596, drawer stack 140 / 285 / 285,
  no front; nine refused edits leave interior, undo label and revision unchanged; undo / redo), hardware (library
  choices, selection changes the generated instances, missing ids reported, refused edit), part overrides
  (material, raw / banded edges, empty override removed, refusal, undo), drawing edits (hit test, view at a
  point, offset / text / hide / remove with undo / redo, stale override after a width change, user dimension
  between corners 820 → 900 mm following the model, refused anchors, orphans and their removal), cutting
  settings (objective, remnant as extra stock, refused remnants / kerf, back to defaults, undo / redo);
  shape editor (`contour_tests.cpp`: rows → spec normalisation, Rectangle = no shape, non-numeric bulge refused,
  round trip of arc / custom / rounded specs, `moveRow`; open-shelving shelf R 30 front corners → outline area
  762 · 280 − 2 · (900 − 225π) mm², refused R 5000 leaves revision and spec unchanged, Remove override keeps the
  contour, undo / redo; panel 600 × 300 with a Ø 40 hole → 180 000 − 400π mm², hole outside refused);
  title, recent files, autosave decisions and
  recovery offers, command labels, every template, Scenario B at the view-model level (width 600 → 700 typed
  into the inspector model: shelf 562 → 662 mm, bottom 664 mm, cut list follows; undo / redo; clean at the save
  point), expressions and refused inputs leave the project unchanged, save / open / broken file, autosave +
  recovery + undo back to disk state, add / duplicate / delete items, selection normalisation, parameter grouping,
  browser tree, diagnostics rows, cutting summary + kerf command, nesting summary of an R 30 shelf (area from
  `domain::cutContour`), nesting settings command / refusal / undo, seed parsing, drawings (A3 sheet, explicit
  scale too large is
  refused, SVG / DXF / PDF export), English captions without a translator.
* `fos_app_gui_tests` (CTest `app_gui_smoke`, label `gui`; run under `xvfb-run`, exit 77 = skipped without a
  display): base cabinet from template, width 900 typed into the inspector (shelf 862 mm), refused input shows an
  inline error, browser lists 10 parts, drawings preview is non-empty, production / cutting views filled, save,
  reopen, width 900. Through the widgets: split the interior vertically, 2 shelves in the left section, door
  pair → 12 parts with sizes checked by hand (partition 684, shelves 271 × 540, doors 716 × 296.5), a refused
  size shows the inline error, undo restores the 10 template parts; hardware combo, part material override,
  a click on the width dimension + text override (recorded at 600 mm, stale at 700 mm), a user dimension by
  two corner clicks (820 mm), cutting objective + remnant. `shape_gui_tests.cpp`: R 30 front corners on an
  open-shelving shelf typed into the Shape section (area as above, "shaped part (routed)"), R 5000 refused inline,
  Reset, drawings preview, circular cut-out added to a free panel through the cut-out table, Cutting lists 2 routed
  parts, the "Router nesting" row is "valid", selecting it shows "Nest sheet 1 of 1" with a non-empty preview, a
  0 mm tool is refused inline ("tool diameter"), 6 mm applied and undone, undo back to 0; screenshots
  `shape-editor-en/uk.png`, `cutting-nest-en.png`. With `FOS_APP_SHOT_DIR=<dir>` it writes 1440 × 900
  screenshots of the four workspaces and of the new editors (part override, interior, front, hardware, drawing
  annotations, cutting remnants) in English and Ukrainian.

## Limitations

* Core diagnostics, part names generated by the furniture module ("Left side") and library names are English
  data, not UI strings; they are not translated.
* Components: no thumbnails, no plugin components (no plugin loading in the app), a component saved to an
  "other" folder is not added to the Preferences list, and inserted items are not linked to their component file.
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
* Nesting: no "back to default nesting settings" button and no editor for the nesting minimum remnant length
  (both available as commands).
* Shapes: free panels cannot be
  created in the UI (only opened from files / added by commands), so their Shape section is reached that way; no
  graphical outline preview inside the Shape editor (the 3D view and part drawings show the result after Apply).
* Only Linux (X11 / offscreen) has been built and tested.
* Updates: in-place installation only on Windows (portable ZIP or the Inno Setup installer); the swap script is
  UTF-8 with `chcp 65001` (paths with exotic characters rely on that); no download resume; ZIP64 archives and
  methods other than stored/deflate are refused; a portable folder in a non-writable location (e.g. Program
  Files) cannot be updated in place. The `.cmd` scripts are unit-tested as text only; they were not executed
  on Windows in this environment.
