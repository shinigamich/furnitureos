<!-- SPDX-License-Identifier: AGPL-3.0-only -->
# 11. Files

| File | Content |
|---|---|
| `.fosp` | Project (all items, libraries, settings, drawing edits) |
| `.foscomp` | Component (a saved cabinet or panel with its materials and hardware) |
| `.foslib` | Library file (materials, edge bands, hardware) |
| `.fosbatch` | Production batch (several projects with quantities) |

## Saving

* **File ▸ Save** (Ctrl+S) / **Save As…**. The file is written safely (a crash never leaves a half-written file),
  and rotating backups `<file>.bak1` … `.bak3` are kept.
* The window title shows `*` while there are unsaved changes; closing asks Save / Discard / Cancel.
* **File ▸ Open recent** lists recently used files.
* A damaged file or a file from a newer version is refused with the reason; it is never partly loaded.

## Autosave and recovery

* **Edit ▸ Preferences ▸ Autosave every** N minutes (default 5, **Off** = 0). The autosave `<file>.autosave` is
  written only when there are unsaved changes. Never-saved projects are autosaved in the application data folder.
* When you open a file and a newer autosave or backup exists, you are asked whether to recover it. The recovered
  state is one undo step: **Undo** returns to the saved file. Save to keep it.
* At start-up, the **Recover unsaved work** dialog offers autosaves of never-saved projects: **Restore**,
  **Discard** or **Keep for later**.

## Components

A component is a cabinet or panel saved with the materials, edge bands and hardware it uses.

* **Save:** select the item ▸ **Edit ▸ Save as component…**: name, category, tags (comma-separated), description,
  author, library folder.
* **Insert:** **Components** dock (next to the project browser): browse by category, filter by tag or search,
  then **Insert** (or double-click). If the project already has a *different* library entry with the same id, choose
  keep the project's entry, replace it, or add the component's entries under new ids. Undo removes the insert.
* **Folders:** **Edit ▸ Preferences ▸ Component folders** (empty = your personal folder).

No thumbnails; inserted items are not linked to their component file.

## Importing a DXF outline

**File ▸ Import DXF outline…** creates a free panel from an ASCII DXF (lines, arcs, circles, polylines; explode
blocks first). Choose name, material (or thickness), layer, units, and whether to skip unsupported cut-outs.
Details: [Getting started](../getting-started.md), "Importing an outline from DXF".

## Exporting the 3D model

**File ▸ Export ▸ 3D model (OBJ)…**: OBJ + MTL in millimetres, Z up, parts as boxes, no machining.

## Comparing two projects

**File ▸ Compare with…** ▸ choose another `.fosp`. The dialog lists what differs from the open project: metadata,
library entries, items (parameters, materials, hardware, interior, placement), settings and the generated parts.
**Export as text…** or **Export as JSON…**. Useful to check what changed between two versions of an order.

## Merging projects (command line only)

To copy items from one project into another use the command line (or save them as components and insert them):

```text
fos merge target.fosp source.fosp --items cab1,cab2 --policy keep -o merged.fosp
```

Clashing item ids are renamed; `--policy keep|replace|rename` decides what happens with library entries that have
the same id but other data; `--offset x,y,z` moves the imported items. Drawing edits and cutting settings of the
source are not imported.

## Production batches

**File ▸ Production batch…** combines several saved projects (orders) into one production run.

1. Toolbar **New** (or **Open…** an existing batch); enter **Batch name**, date and notes.
2. **Projects** page: **Add project…** or **Add current project** (save it first). Edit **Key**, **Quantity** and
   **Note** in place.
3. **Settings** page: panel saw settings for all materials (kerf, trims, objective), **Material substitutions**
   (replace a material id by your stock material; **Apply substitutions**) and **Group identical parts across items
   in the cut list**.
4. **Summary** page: **Recalculate** reloads all projects from disk; materials, sheets (lower bound), edge banding,
   hardware and problems. Export: **Export cut list (CSV)…**, **Export labels (SVG)…** (with batch, project and
   copy on each label; barcodes as in [chapter 7](07-production.md#part-labels-with-barcodes)), **Export cutting
   plans (SVG)…**.
5. **Save** the batch (`.fosbatch`, project paths stored relative to it).

A missing or broken project, a library conflict or a refused substitution is listed as "Incomplete batch" / "LEFT
OUT"; the other projects are still produced. Batch quote: command line only (`fos batch quote`).
