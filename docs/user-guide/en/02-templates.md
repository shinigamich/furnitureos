<!-- SPDX-License-Identifier: AGPL-3.0-only -->
# 2. Creating furniture from templates

## Start a project or add a unit

* **File ▸ New from template ▸ …** starts a new project with one unit.
* **Edit ▸ Add cabinet ▸ …** adds a unit to the open project, to the right of the existing ones.
* **Edit ▸ Duplicate item** (Ctrl+D) copies the selected item; **Edit ▸ Delete item** (Del) removes it.

Steps:

1. Choose the template.
2. In the dialog enter **Width**, **Height (including plinth)** and **Depth** in mm. The fields start with the
   template's own default size.
3. Press **OK**. All other parameters keep the template defaults; change them later in the inspector
   ([chapter 3](03-editing-cabinets.md)). An impossible size is refused with the reason.

New projects use **generic placeholder materials and fittings without prices**. Set up your real boards, edge
bands and prices in **Project ▸ Libraries…** ([chapter 4](04-materials-and-libraries.md)).

## Templates in the desktop application

| Template | Default W × H × D (mm) | What you get |
|---|---|---|
| Base cabinet | 600 × 820 × 560 | Kitchen base unit with door pair and adjustable shelf |
| Wall cabinet | 600 × 720 × 320 | Wall unit with a single door and two shelves |
| Tall cabinet | 600 × 2100 × 560 | Tall unit: two stacked sections with shelves behind one door |
| Drawer unit | 600 × 820 × 560 | Drawer stack, **fronts only** (no boxes) |
| Drawer unit with boxes | 600 × 820 × 560 | Same, with a drawer box on generic ball-bearing slides behind every front |
| Wardrobe section | 1000 × 2200 × 580 | Two columns (shelves left, one shelf right) behind a door pair |
| Open shelving | 800 × 1800 × 300 | Two stacked sections with shelves, no fronts |
| Blind corner cabinet | 1000 × 820 × 560 | Base corner unit with a 500 mm fixed blind panel on the left |
| L-shaped corner cabinet | 900 × 820 × 560, leg B 900 × 560 | 90° L-shaped base corner unit with two independent doors |
| Tall oven housing | 600 × 2100 × 560 | Two drawers, an open oven niche (595 mm high) and a door above |
| Dishwasher gap with decor front | 618 × 820 × 560 | 600 mm appliance gap, end panel on the right, decor front fixed to the appliance door |

The default sizes are those reported by `fos new --help`; the dialog always shows the current defaults.

### Corner units

Select the corner cabinet. The **Parameters** tab has a **Corner** group:

* **Blind corner:** **Blind side** (left or right, seen from the front; the door is hinged on the other side) and
  **Blind panel width**.
* **L-shaped corner:** **Width** and **Depth** are those of leg A (rear wall); **Leg B length** and **Leg B depth**
  set the leg along the left wall.

Only blind corners and 90° L-shapes exist. Bi-fold corner hinges are not modelled.

### Appliance units

* **Tall oven housing:** the niche is a section with the front type **Appliance niche (open)**. Under **Niche
  ventilation** choose none, a slot in the shelf below, or a cut-out in the back panel, with its size and distances.
* **Dishwasher gap:** no carcass, only end panel(s) and a decor front that is fixed to the appliance door.

> **Appliance sizes are generic.** Niche height, gap width and ventilation are never matched to a real appliance.
> Always check the appliance's installation sheet. No plinth notching, appliance fixings or worktop support over
> the gap are generated.

### Wardrobes

Start from **Wardrobe section** and build the interior on the **Interior** tab: split into columns, add shelves, a
**clothes rail**, **inner drawers** behind the doors, or **sliding doors** ([chapter 3](03-editing-cabinets.md)).
For a wider wardrobe, add several sections side by side (**Edit ▸ Add cabinet**) and align them in the **Plan** tab.

### Desks and tables (command line only)

Desk and table templates exist in the engine, but **not yet in the desktop menus**. Create them with the
command-line tool, then open the file in the desktop application:

```text
fos new desk.fosp --template desk       (1400 × 740 × 700: end panels, modesty panel, cable slot, 3-drawer pedestal on the right)
fos new table.fosp --template table     (1600 × 750 × 800: top on four generic legs)
```

In the desktop application you can change the general parameters (width, height, depth, …), materials and produce
all documents. Desk options (legs or panel ends, modesty panel, cable slot, pedestal) cannot be changed in the
desktop window yet. The legs are **generic** fittings: check the real legs' data sheet.

## Free panels

A free panel (for example a single shelf or a shaped board) is created with **File ▸ Import DXF outline…**
([chapter 11](11-files.md#importing-a-dxf-outline)), by a worktop or plinth ([chapter 5](05-kitchen-tools.md)),
or comes from a component.
