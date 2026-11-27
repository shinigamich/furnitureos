<!-- SPDX-License-Identifier: AGPL-3.0-only -->
# 10. CAM (experimental, command line only)

> ## Safety warning: read before any use
>
> * **No machine is verified.** No CNC program from FurnitureOS has been run on, or proven against, any real
>   machine or controller.
> * The only machine profile is the **fictional "FurnitureOS demo profile — not a real machine"**; the default tool
>   library is fictional too. Every output is marked `verifiedForMachine: false`.
> * Hardware drilling positions are **generic** and not taken from manufacturer data.
> * A wrong program can destroy the machine, the tool or the workpiece and can **injure people**.
>
> **Never run generated code unchecked.** A qualified CNC operator must review every line, check work zero, tool
> lengths, tool change, spindle direction and clamps, and prove the program on the actual control: dry run above
> the stock, reduced feed, hand on the emergency stop. FurnitureOS never sends anything to a machine.

The desktop application has **no CAM functions**. Experimental G-code generation exists only in the command-line
tool `fos` (`fos.exe` on Windows).

## What it does

`fos cam` writes a 3-axis program (generic ISO G-code) for the operations on **one face of one part**:

```text
fos cam kitchen.fosp cab1/side.left --face B --out side-left-B.nc
```

* The part id is shown in the inspector (**Id**) when you select a part.
* The program is simulated independently first; on any simulation error **no program is written**.
* The report lists what is machinable in this setup, what needs the other face up, and what is **UNSUPPORTED**
  (edge holes need a horizontal spindle or aggregate; some pockets). Nothing is dropped silently.
* A "Review before use" list is printed with every program (work offset, tool length compensation, M6 behaviour,
  spindle direction and spin-up, clamps).
* Shaped parts are routed along their outline; sharp inside corners keep the tool radius; no tabs, ramps or
  part-in-part ordering.

Options: `--peck DEPTH`, `--blank-allowance MM`, `--tools FILE.fostools` (your own tool table; the machine profile
stays the fictional demo profile). Tool tables: `fos tools export-default -o tools.fostools` (fictional starting
point), `fos tools validate`, `fos tools list`.

## Not supported

Real machine profiles, proprietary formats (Homag, Biesse, SCM and others), sending to a machine, edge drilling.
