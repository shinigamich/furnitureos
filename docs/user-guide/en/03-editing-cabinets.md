<!-- SPDX-License-Identifier: AGPL-3.0-only -->
# 3. Editing cabinets

Select the **cabinet** (not one of its parts) in the browser or the 3D view. The **Properties** panel shows the
name and three tabs: **Parameters**, **Construction**, **Interior**. Part sizes always follow from these settings;
you never type a part size directly.

## Parameters tab

1. Find the parameter (grouped by kind; use **Filter parameters** to search by name).
2. Type a number (`900`, `812.5`, `812,5`) or an expression over other parameters (`depth + 140`, `width / 2`).
3. Press **Enter**.

* Parameters in *italics* are **derived** (calculated from others). Typing a value **pins** it; **Reset** returns
  to the standard formula.
* A refused value (unknown name, syntax error, a size that cannot be built) is shown in red and nothing changes.
* Each accepted change is one undo step; the Edit menu names it, e.g. "Undo Set width of Base cabinet to 900".

## Construction tab

* **Construction**: top and bottom joints, top panel or rails, back panel mounting, plinth (feet, plinth panel
  with sides to the floor, or none), top, bottom and divider fasteners (confirmat screws or dowels), **Drill shelf-pin holes**.
* **Materials**: **Carcass**, **Back panel**, **Fronts**, **Drawer boxes**, **Drawer bottoms** and the edge bands
  for **Visible edges**, **Hidden end edges** and **Front edges**, chosen from the project library.
* **Hardware**: **Hinge**, **Handle**, **Shelf pin**, **Confirmat screw**, **Dowel**, **Foot**, **Sliding system**,
  **Cabinet hanger** (optional) and **Wall rail**. Only fittings of the matching kind from the project library are
  offered. A cabinet hanger needs a grooved back with enough groove distance (or no back); the panel tells you the
  required distance.

## Interior tab

The interior is a tree of **sections**: "Whole interior" at the top, below it the sections of each split, with
size and content. Select a section first, then use the controls below.

### Splitting into sections

1. Set **Into sections** (2–9).
2. Press **Split vertically** (side by side, separated by partitions) or **Split horizontally** (stacked,
   separated by fixed shelves). The new sections get equal shares.
3. To size a section: select it, type in **Size** and press **Set size**:
   `300` = 300 mm fixed, `1*` / `2*` = share of the remaining space, other text = expression (`width / 3`).

**Remove split** turns a split section back into one empty section (its front is kept). **Empty interior** removes
everything (sections, shelves, fronts).

### Shelves

Select a section that is not split and set **Shelves** (adjustable shelves, evenly spaced).

### Clothes rail

1. Select a section that is not split.
2. Tick **Clothes rail**, set **Height from top** (rail axis below the panel above) and choose the **Rail** and
   **Supports** from the library.
3. Press **Apply clothes rail**.

Refused (with the reason): a section that is split, has adjustable shelves, is less than 500 mm deep, leaves
less than 800 mm hanging space below the rail, or has a front other than door, door pair or sliding doors. These
limits are generic rules of thumb.

### Fronts

Select the section, choose **Type** under **Front of the section**, fill the fields and press **Apply front**.

| Type | Notes |
|---|---|
| **No front** | Open section. |
| **Single door** | Hinged left or right; overlay; **Cup hinges**; handle position. |
| **Door pair** | Two doors over the opening. |
| **Drawer stack** | **Drawer heights** bottom to top, separated by `;`, e.g. `140; 1*; 1*`. |
| **Lift-up flap** | Hinged at the top edge; one generic lift mechanism per side. Not possible in a section closed by stretcher rails; a cabinet too low, too tall or too shallow for the lift is refused. |
| **Sliding doors** | Number of doors 2–6, or an expression such as `if(width > 2000, 3, 2)`. Inset doors in a generic two-lane track (**Sliding system** on the Construction tab). A sliding section cannot hold other fronts. |
| **Appliance niche (open)** | Open niche for an appliance, with optional ventilation ([chapter 2](02-templates.md#appliance-units)). |
| **Appliance decor front** | A front fixed to an appliance door, no hinges. |

**Overlay**: full overlay, half overlay, or inset (inside the opening). **Handle**: none, centred, top, bottom.

### Drawer boxes

For a **Drawer stack**, set **Drawer boxes** to **Box on slides** to get a box (two sides, inner front, back,
bottom) behind every front, carried by two **Slides** (library entries of kind drawer slide: ball-bearing or
undermount), with the **Drawer bottom** in grooves or nailed under. The box size follows the opening, the cabinet
depth and the slide rules. Impossible combinations (nailed-under bottom on undermount slides, cabinet too shallow
for the shortest slide) are refused. Box materials: **Drawer boxes** and **Drawer bottoms** on the Construction tab.

### Inner drawers (behind a door)

For wardrobes and pantry units: drawers inside a section that is **not split**, behind a door, door pair or sliding
doors (no adjustable shelves or rail in that section).

1. Tick **Drawers behind the door**.
2. **Heights**: one compartment per drawer, bottom to top, e.g. `200; 1*; 1*`.
3. Choose **Slides**, **Drawer bottom**, **Box joints**, **Front** (box front only, or a facia in front of the box)
   and the **Facia handle** (grip cut-out or none).
4. Press **Apply inner drawers**.

### Frame-and-panel fronts

For a single door, door pair, flap or drawer stack set **Style** to **Frame and panel** (instead of **Slab**):
two stiles, two rails and a panel in a groove. Set **Stile width**, **Rail width**, **Panel thickness**, **Groove
depth**, **Panel clearance**, **Frame joint** (stub tenon or dowels) and **Panel material** (empty = the front
material, with a thickness warning). The frame values are generic defaults. Not available for sliding doors,
appliance fronts, L-shaped corners or shaped fronts.

**Glass-panel fronts:** the engine can build frame fronts with a glass pane in a rebate (the pane is listed as a
bought-in item, [chapter 7](07-production.md#bought-in-items)), but the desktop application **cannot switch a front
to glass yet**. Such fronts appear only in projects or components made elsewhere.

## A single part: facts, overrides and shapes

Select a part to see its finished size, cut size (finished size minus edge band thickness), material, grain,
edges and number of machining operations, plus its **Id** (needed for some command-line tools).

* **Override for this part**: another material, and each edge L1 / L2 / W1 / W2 as "Cabinet rule", raw (no edge
  band) or a library edge band. **Remove override** returns to the cabinet rules.
* **Shape**: rounded corners, an arc edge or a custom outline, plus rectangular or round cut-outs; press **Apply**.
  Shaped parts are routed, not sawn ([chapter 8](08-cutting-and-nesting.md#router-nesting-for-shaped-parts)).
  Details: [Getting started](../getting-started.md), section 5.
