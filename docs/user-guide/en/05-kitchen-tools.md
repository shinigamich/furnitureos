<!-- SPDX-License-Identifier: AGPL-3.0-only -->
# 5. Kitchen tools

Worktops and plinths are added over a **straight run** of cabinets that share the same rotation, do not overlap and
stand level (within 1 mm). They become ordinary free panels, so they appear in the cut list, cutting plans,
drawings and quote.

> Worktops and plinths **do not follow later cabinet edits**. After changing the run, delete them and add them
> again.

## Worktop

**Edit ▸ Add worktop…**

1. Under **Cabinets of the run** tick the cabinets (the first ticked one is the reference).
2. Set **Front overhang** (default 20 mm), **Left overhang**, **Right overhang**, **Gap to the wall**.
3. Choose **Material** (if none is chosen, a generic 38 mm worktop board, stock 4100 × 650, cost 0, is added to the library) and
   **Edge band**; tick the **Banded edges** (front edge, back edge, left end, right end).
4. **Cut-outs (sink, hob)** ▸ **Add cut-out**: kind (sink or hob), **Centre x** (from the left face of the leftmost cabinet),
   **Length**, **Depth**, **Corner radius**, **Front setback** (or **Centred**).
5. Check the **Preview** and **Longest piece** against your **Stock length**, then **OK**.

* A run longer than the stock is split into equal pieces with plain **butt joints** (no mitres, no connector-bolt
  pockets).
* Minimum distances are rules of thumb: 50 mm from a cut-out to the front/back edge, 100 mm to a free end,
  150 mm to a joint, 100 mm between cut-outs. A violation is refused with the distances. Always check the sink and
  hob installation sheets.

## Plinth (toe-kick)

**Edit ▸ Add plinth…** (cabinets must stand on feet; appliance gaps are allowed).

1. Tick the **Cabinets of the run**.
2. **Setback** behind the foremost carcass front (default 50 mm, rule of thumb).
3. **Height**: **From the cabinets** or a value (not higher than the cabinets' plinth height).
4. **Material** (default: the carcass material of the first cabinet).
5. **End returns**: **Left** and/or **Right**.
6. Check **Preview** and **Longest piece**, then **OK**.

The plinth is interrupted at dishwasher end panels. Long pieces are split with butt joints; plinth clips and
connectors are not modelled. Integrated dishwashers often need a lower or notched plinth: check the appliance.

## The Plan (room layout)

The **Plan** tab shows all items from above.

* **Drag** an item to move it. It snaps to other items' edges and to room walls; otherwise moves in 1 mm steps.
  Gaps to the right neighbour and to the walls are shown.
* **Rotate 90°** turns the selected item counter-clockwise about its centre.
* **Add room**: inner width (**W**), depth (**D**), optional height (**H**), position (**X**, **Y**) and **Wall**
  thickness (drawing only). **Remove room** deletes it. The room is saved with the project.
* **Fit**, **Zoom in/out** (or mouse wheel), middle/right drag to pan. **Export SVG…** saves the plan (1 unit = 1 mm).

Each move or rotation is one undo step. Footprints exclude handles and feet.
