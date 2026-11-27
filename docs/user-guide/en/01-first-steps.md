<!-- SPDX-License-Identifier: AGPL-3.0-only -->
# 1. First steps

## Installing

* **Windows:** see [Installing on Windows](../installing-windows.md). The portable build needs no installation:
  unzip the folder and start `furnitureos.exe`. If the 3D view stays empty on an older or virtual PC, start the
  program with software rendering (`set QT_OPENGL=software`, then `furnitureos.exe`).
* **Linux (build from source):** see [Getting started](../getting-started.md), section 1.

**Language:** the program follows the system language (Ukrainian or English). To change it: **Edit ▸ Preferences ▸
Language**, then restart. Messages from the engineering checks and generated part names ("Left side") are always
in English.

## The main window

| Area | What it is for |
|---|---|
| **Project browser** (left dock) | Tree of the project: item ▸ assembly ▸ parts with finished size L × W × T. Click to select. |
| **Components** (tab next to the browser) | Your library of saved cabinets and panels ([chapter 11](11-files.md#components)). |
| **Workspace tabs** (centre) | 3D, Drawings, Production, Cutting, Quote, Packing, Plan. |
| **Properties** (inspector, right dock) | Settings of the selected cabinet, panel or part. |
| **Diagnostics** (dock) | Errors and warnings of the engineering checks; the status bar shows the counts. Click a finding to select the part. |

Show a hidden dock again from the **View** menu (bottom entries).

## Workspaces

| Tab | Content |
|---|---|
| **3D** | The model. Selection is shared with the browser. |
| **Drawings** | Assembly, isometric, exploded and part drawings; export and print ([chapter 6](06-drawings.md)). |
| **Production** | Cut list, parts, materials, edge banding, banding jobs, hardware, machining, bought-in items ([chapter 7](07-production.md)). |
| **Cutting** | Sheet layouts for the panel saw and router nesting ([chapter 8](08-cutting-and-nesting.md)). |
| **Quote** | Customer price estimate ([chapter 9](09-quotes.md)). |
| **Packing** | Packing list, package labels, assembly sheets ([chapter 7](07-production.md#packing-list-and-assembly-sheets)). |
| **Plan** | Top view of all items, with an optional room; move and rotate items ([chapter 5](05-kitchen-tools.md#the-plan-room-layout)). |

Everything in every workspace is recalculated automatically after each change and after Undo.

## Working in the 3D view

* **Left drag**: orbit. **Middle drag** or **Shift + left drag**: pan. **Wheel**: zoom to the cursor.
* **Click** a part to select it; **Ctrl+click** or **Shift+click** adds parts to the selection.
* **View ▸ View presets**: front, back, left, right, top, bottom, isometric. **F** = fit all.
* Display: **1** shaded, **2** shaded with edges, **3** wireframe, **4** realistic materials (colours, grain,
  edge bands). **P** perspective, **O** orthographic.
* **Measure (M)**: click two points (corners, edge midpoints, face centres snap). The status bar shows the
  distance and ΔX / ΔY / ΔZ. **Esc** clears the points, a second **Esc** leaves measuring. Nothing is changed.
* **Section plane (S)**: cut the view along X, Y or Z with a slider; **Flip** or **Off**. Display only.
* **Resize handles**: when a cabinet or panel is selected, drag the arrows on its faces to change width, depth or
  height (1 mm steps, **Shift** = 10 mm, **Esc** cancels). Dragging the left or back face keeps the opposite face
  in place. One drag = one undo step.

## Your first cabinet in five steps

1. **File ▸ New from template ▸ Base cabinet…**
2. Enter width, height (including plinth) and depth, press **OK**.
3. Click the cabinet in the browser; change a value on the **Parameters** tab and press **Enter**.
4. Look at the **Production** tab (cut list) and the **Cutting** tab (sheet layouts).
5. **File ▸ Save** (`.fosp` file).
