# CAM and postprocessor survey

Date: 2026-11-11. Limited web access: only a few facts were verified in this session; the rest is general knowledge and is labelled. Status labels: **documented** (public specification or manual known to exist), **partially documented**, **proprietary / unknown** (no public specification confirmed). None of the classifications below was verified against vendor documentation in this session unless a source is cited. Do not treat them as certification.

## 1. Controller and machine-software landscape

### 1.1 G-code dialect families (nesting routers, general CNC)

| Family | Typical use | Documentation status (general knowledge) |
|---|---|---|
| Fanuc-style ISO G-code (also Mitsubishi, Okuma variants) | Industrial machining centres | Partially documented: programming manuals exist but are controller- and option-specific; dialect differences are substantial. |
| Siemens Sinumerik (ISO and Siemens-native) | Industrial routers/machining centres | Documented in vendor manuals (registration may be required). |
| Syntec | Common on Asian-built nesting routers | Partially documented; manuals exist but not verified here. |
| NC Studio / Weihong (NcStudio) | Low-cost routers | Partially documented; manuals typically supplied with machine. |
| Mach3 / Mach4 | Hobby and small-shop routers | Documented (user manuals, macros), dialect is a subset of RS-274 with extensions. |
| LinuxCNC (RS-274/NGC) | Open-source controller | Documented: public user documentation; the interpreter is open source (GPL family; repository page showed GPL-2.0 sidebar with mixed LGPL-3/GPL-2 badges, licence details need checking in `COPYING`). |
| GRBL | Small hobby routers | Documented (open source; limited G-code subset, no tool change). |

### 1.2 Proprietary machine software / formats for furniture machines

| System | Format | Status |
|---|---|---|
| Homag woodWOP | MPR (parametric text-like macros) | Proprietary; partially known from third-party descriptions; vendor specification not verified. |
| Biesse | BiesseWorks CIX / BPP, bSolid | Proprietary; unknown here. |
| SCM | Xilog XXL, PGMX | Proprietary; unknown here. |
| Felder | Machine-specific (several controllers) | Unknown. |
| IMOS, ix (software suites) and other exchange layers | Product-specific | Proprietary; do not assume interoperability. |

Machine makers sometimes document formats for partners under agreement; the correct route is a written request.

## 2. Recommended architecture

```
domain::Operation (machine independent: holes, grooves, pockets, contours)
   -> toolpaths (tool, strategy, feeds, lead-ins, ordering)  [cam]
   -> machine profile (frame, limits, tools, spindle, M-codes, safe heights, units, verified flag)
   -> postprocessor (one per dialect; pure function of toolpaths + profile)
   -> mandatory validation (limits, collisions with clamps/table, tool vs. operation, missing data)
   -> human review (preview, simulation, summary of assumptions, explicit sign-off)
   -> file
```

Principles:
* The model stays machine independent; machine specifics exist only in profiles and postprocessors (ARCHITECTURE.md 4.2: machine frame conversion happens only in `cam`).
* A postprocessor never invents data. Missing information (tool numbers, feeds, offsets) is an error, not a default.
* Profiles and postprocessors carry a `verified` flag and provenance; unverified output is labelled in the file header and in the UI, per the honesty rule.
* Generated files are reviewed by a person before running on a machine; the first run is done with a dry run above the table, with the operator at the emergency stop. The software must say so.
* Postprocessors are first-party, reviewed code in phase 1 (ADR-0007). Plugins producing machine code, if ever allowed, must go through the same validation.
* Every postprocessor has golden-file tests from known inputs and, where possible, an independent consumer (below).
* Safety limits (travel limits, maximum depth, minimum tool diameter vs. hole) are enforced in validation and are not user-overridable without an explicit unsafe flag recorded in the file.

## 3. First adapter that can be verified without a machine

**LinuxCNC (RS-274/NGC).** Reasons:
* A standalone interpreter binary `rs274` exists (Debian man page `rs274(1)` in package `linuxcnc-uspace`: "standalone G-code interpreter", options such as `-g file.ngc -t tool.tbl`; seen in search results, page not opened in full).
* LinuxCNC ships simulator configurations under `configs/sim/` that run without hardware (search results show the AXIS sim INI files). Output can therefore be parsed and executed in simulation on a developer machine or CI runner (installation and CI feasibility not tested in this session).
* The dialect is publicly documented.

What this verification proves: the file is syntactically valid RS-274 and runs to completion in simulation, and that simulated positions match the model within tolerance. It does **not** prove safe behaviour on a real machine, correct tool offsets, spindle direction/M-codes of a specific controller, or collision-freedom.

Not claimable without physical machines or vendor tools: Fanuc/Siemens/Syntec/NC Studio/Mach postprocessors, Homag/Biesse/SCM formats, drilling-machine formats. For these the product may offer only "experimental, unverified" output, and only after specification review. A GRBL dialect could be validated with an open-source simulator or a controller on a bench without a spindle, but that was not evaluated here.

## 4. Roadmap

1. Machine-independent toolpath generation and a visual preview.
2. LinuxCNC postprocessor with simulation-based test in CI (if installable on the runner).
3. Mach3/Mach4 and GRBL variants as profile-level differences, labelled unverified until tested on equipment.
4. Vendor formats only with vendor specifications and test access.

## 5. Open questions

* Which machines target users own (drives priority).
* Whether `rs274` is available or buildable on supported CI images.
* Legal terms for any vendor format specification obtained.
