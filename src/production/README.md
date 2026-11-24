# `production` — production documentation (reports, cut lists, labels, cost, quotes)

Pure functions of a read-only `domain::Model`; see the doc comments of the public headers in
`include/fos/production/` (mm unless a field ends in `M` / `M2`).

## Customer quotes (`fos/production/quote.hpp`)

`core::Result<Quote> quote(const domain::Model&, const PricingSettings&, const QuoteOptions& = {})`

* The model's libraries carry the prices: `Material::costPerSquareMeter` / `costPerSheet`,
  `EdgeBand::costPerMeter`, `HardwareDefinition::unitCost` (0 = unknown).
* Materials by AREA (default): cut area of the blanks × (1 + `wastePercent`/100) × price per m²
  (or sheet price / sheet area, noted on the line). By SHEETS when `QuoteOptions::sheetUsage`
  lists the material (e.g. from a cutting plan): sheets × sheet price (or price per m² × sheet area).
* Edge bands: purchase length (`edgeBandingReport`, with its allowances) × price per metre.
  Hardware: quantity × unit price, in pieces. A definition sold per metre (`custom["unit"] = "m"`,
  `isPricedPerMetre`) has `unitCost` **per metre**: it is quoted as total length (sum of
  `HardwareInstance::length` × quantity) in "m" × unit price, and is UNPRICED when any of its
  instances has no length. Lines of linear fittings note "N pc, total length X m".
* Labour, one line per set rate: cut metres (saw cuts of the given sheet usage + routing length
  of shaped parts; unpriced without sheet usage), applied edge metres, holes, parts.
* `markup = subtotal × markupPercent/100`, `tax = (subtotal + markup) × taxPercent/100`.
* A line without a price is kept as UNPRICED (amount 0, warning `production.quote.unpriced`,
  `Quote::complete == false`); prices are never invented. Currency is a label only.
* `quoteTable`, `quoteText`, `formatMoney` (2 decimals, locale-independent) render it.

Limitations: no per-material waste factors, no rounding of quantities to purchasable units
(by-area quotes price fractional m²), no edge-band roll rounding, no discounts.

## Packing lists (`fos/production/packing.hpp`)

`core::Result<PackingList> packingList(const domain::Model&, const PackingOptions& = {}, const Scope& = {})`

* Per assembly: packages of panels (max weight `maxPackageWeightKg`, default 25 kg; optional outer
  length / width / height limits; `padding` per side, default 10 mm) and one `HardwareBag`
  (`hardwareReport` per assembly, split into fittings and fasteners = Confirmat / Dowel /
  CamConnector / Screw).
* Panel weight = finished area (exact outline area for shaped parts) × thickness × `Material::density`
  (kg/m³). Density 0 or a missing material → weight UNKNOWN (never estimated); such panels are packed
  in their own packages (`weightKg` empty, "weight unknown"), with `production.packing.density_unknown`.
* Rule (heuristic): sort by longer side, shorter side (desc), part number; first fit per weight class.
  A panel that alone breaks a limit is packed alone, flagged `overweight` / `oversize` with a warning.
* `packingListText`, `packingTable` (CSV via `toCsv`), `renderPackageLabelsSvg` (label sheet geometry
  of `LabelSheetOptions` / `labelGrid`).

Limitations: weights exclude packaging and hardware (no hardware weights in the catalogue); no
3D bin packing (stacks only, panels flat); unknown-weight panels are limited only by the size limits.

## Assembly sheets (`fos/production/assembly_sheet.hpp`)

`AssemblySheets assemblySheets(const domain::Model&, const Scope& = {})`,
`assemblySheetText`, `assemblySheetSvg`.

GENERIC order from part roles (Carcass: Side/Top/Bottom/Rail/Plinth → Back → Shelf/Partition →
drawer box parts → Door/DrawerFront → handles → Worktop/Generic) and hardware kinds; fasteners go to
the latest step among the parts they connect. Not derived from joint analysis; every sheet prints
`kAssemblyOrderNote`. SVG is text only (no exploded views).

## Linear hardware (wall rails, sliding-door tracks)

`domain::HardwareInstance::length` (mm per piece, 0 = not linear) is summed per definition by
`hardwareReport`: `HardwareRow::linearPieces` (pieces with a length) and `HardwareRow::totalLengthM`
(metres). `HardwareRow::perMetre` / `isPricedPerMetre(def)`: the definition's `custom["unit"]` is
`"m"`, so `unitCost` is per metre and `totalCost = totalLengthM × unitCost` (cost unknown and
warning `production.hardware.missing_length` when an instance has no length). A negative or
non-finite length is ignored (`production.hardware.invalid_length`). `hardwareTable` adds a
"Total length (m)" column (after Qty) only when the report contains a linear fitting, so other
reports keep their layout; per-metre unit prices read "12.5 / m". `costTable` shows per-metre rows
in metres; `CostEstimate::HardwareCostLine` carries `perMetre` / `totalLengthM`. Packing bag lines
carry `BagLine::totalLengthM` and the text / CSV show "total length X m".
