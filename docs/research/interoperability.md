# Interoperability survey

Date: 2026-11-11. Scope: exchange with furniture design and cutting software and neutral formats.

**Verification policy.** Web access in this session was limited (GitHub pages and a search tool worked; vendor sites and documentation portals were mostly not reachable). Anything not backed by a source named here is marked **unverified** and must be checked against the vendor's current documentation or by testing with a legally obtained copy before it is relied on. Product names are trademarks of their owners; FurnitureOS is not affiliated with any of them.

## 1. Legal and ethical ground rules

* Use **documented interchange only**: public specifications, formats the vendor documents or explicitly offers for export, and neutral standards.
* **No reverse engineering of protected or undocumented formats**, no circumvention of licence checks or encryption, no redistribution of vendor files, libraries or sample data without permission. Reverse engineering may be restricted by the product's EULA and by local law; where interoperability exemptions exist (for example in EU software law) they are narrow and need legal advice. This project does not rely on them.
* Ask vendors for specifications or partnership where a format matters. Record any permission in writing in the repository.
* Do not claim support for a product's format unless it has been tested on real files produced by that product; use wording such as "reads CSV that Product X can export" only after testing.

## 2. Proprietary products

| Product | Publicly documented (what could be found) | Known import/export formats | Legal constraints | Recommended approach |
|---|---|---|---|---|
| **bCAD** (bCAD Furniture / bCAD Мебель) | Not verified in this session. | Unverified. | Proprietary; EULA not reviewed. | Do not target native files. Offer neutral exports (DXF for contours, CSV for lists) that users can import using the product's own documented import features, if any. Verify by reading the vendor manual first. |
| **Astra** (Astra Furniture Constructor, Astra Cutting / Astra Раскрой) | Not verified. A search in this session returned nothing authoritative about Astra exchange formats. | Unverified. | Proprietary. | Same as bCAD: CSV/DXF export first; investigate whether the vendor documents a text import for cutting lists. |
| **PRO100** | A third-party library document (Elixir `PhoenixKitCatalogue.Export.Pro100`) describes a text export for PRO100: UTF-8 with BOM, tab-separated, CRLF line ends, numeric IDs required. This is a third-party description, not an official PRO100 specification. Blum states it offers a data interface for PRO100 and a Blum BXF import add-on exists for PRO100 (from search snippets; not opened or confirmed on the vendor pages). | Library/catalogue import via text files appears to exist (see above). Other formats unverified. | Proprietary; Blum BXF is a Blum-defined format; check Blum's terms before using it. | Candidate: export a materials/components catalogue as the tab-separated text format after checking PRO100's own manual and testing with the real program. Treat as export-only. |
| **GibLab** (online cutting/estimation service) | Not verified; a search returned nothing about GibLab. | Unverified (online services commonly accept CSV/Excel lists; do not assume). | Online service terms of use apply; automated access may be restricted. | Provide a configurable CSV cutting list (columns, units, rotation flags) that the user uploads manually. Do not scrape or automate the web service. |
| **ViyarPro** (Viyar online service) | Not verified. | Unverified. | Online service terms apply. | As GibLab: manual CSV/DXF export only; revisit if a public API or specification is published. |
| **Basis (Bazis) Cutting / Basis-Furniture Maker** (mentioned in search results; not in the requested list but relevant) | A third-party 1C integration description says Basis-Cutting exchange with 1C uses files (TXT, XML, DBF) and FTP; matching by barcode (secondary source, Infostart; not verified against the vendor). | Text/XML/DBF per that source. | Proprietary. | If demand exists, study the vendor's own documentation of its exchange files. |
| **Blum BXF / other hardware-maker data** | Blum publishes information about a CAD/CAM interface for partner software (search result titles; contents not verified). | Unverified. | Hardware-maker terms of use and data licensing apply. | Possible source of verified hardware dimensions via official data; needs licence review. |

## 3. Neutral formats

| Format | Use | Status in FurnitureOS (planned) | Notes |
|---|---|---|---|
| **DXF** (Autodesk Drawing Exchange Format) | 2D drawings, part contours, drilling points; accepted by many CAM and cutting tools | Planned (`formats`, `drawings`) | Publicly documented by Autodesk, but versions differ (R12 ASCII is the most portable). Specify layers, units (mm), arcs and polylines; test import with free viewers (e.g. LibreCAD, not tested here). |
| **SVG** | Drawing and nesting preview; Deepnest/SVGnest style nesting input | Planned for drawings | W3C standard; exact-scale output needs explicit `mm` units and viewBox. |
| **PDF** | Print drawings and labels | Via Qt `QPdfWriter`/printing module | Exact scale requires disabling page fit; verify by printing and measuring. |
| **STEP (ISO 10303-21, AP203/AP214/AP242)** | 3D exchange of solids | Not supported until an OCCT backend exists (ADR-0002) | Standard is paid-for by ISO; OCCT implements import/export. |
| **STL** | Mesh preview, 3D printing, some viewers | Possible as simple export of box meshes | No units or colours; mesh only. |
| **OBJ** | Mesh with materials | Planned export (listed in ARCHITECTURE.md `formats`) | Unit-less; define mm in the export header comment. |
| **CSV** | Cutting lists, BOM, hardware lists | Planned (`production`) | Define delimiter, decimal separator, encoding (UTF-8, optional BOM for spreadsheet programs) explicitly; locale-dependent decimal commas are a frequent failure. |
| **Cutting-list exchange formats** | Cutting optimisers and panel saws | See below | No single dominant public standard identified in this session. |

### Cutting-list exchange

* Search results in this session show cutting optimisers (OptiCut, MaxCut, optiCutter and others) importing **CSV/Excel** lists with configurable column mapping, and OptiCut accepting **DXF** parts. These are secondary sources (vendor forum/help pages as returned by search; pages not individually read).
* Panel saws and optimisers from machine makers often use proprietary job formats; whether open specifications exist for particular saw makers was **not verified**.
* Recommendation: one canonical CSV (id, name, material, thickness, cut length, cut width, quantity, grain/rotation, edge codes for L1/L2/W1/W2, notes) plus a column-mapping profile so users can match the target program. Cut sizes come from `domain::cutSize()` (ADR-0004), and the header states units.

## 4. Prioritised adapter roadmap

1. **CSV cutting list and BOM** (configurable columns, UTF-8). Highest value, lowest risk, fully testable.
2. **DXF export of part contours and drilling layers** (R12 ASCII first), with verification by re-import in an independent open-source CAD program.
3. **SVG and PDF drawing output** with exact-scale tests.
4. **DXF import of 2D contours** for custom part shapes (nesting input).
5. **OBJ/STL export** of assembled model for viewing.
6. **Library import/export** (materials, edge bands, hardware) in FurnitureOS' own declarative formats (ADR-0007), plus CSV catalogue import.
7. **Vendor-specific text exports** (for example the PRO100 catalogue text format), only after reading the vendor's documentation and testing with the product.
8. **STEP** via the optional OCCT backend.
9. Anything else (proprietary machine/CAD formats) only on vendor-supplied specifications.

## 5. Open questions

* Which of bCAD, Astra, GibLab, ViyarPro publish import specifications or accept CSV/DXF: needs vendor documentation or user reports.
* Whether Blum or other hardware makers allow redistribution of their data (BXF etc.).
* Which target audience's tools matter most (to be decided by product owner).
