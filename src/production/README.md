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
  Hardware: quantity × unit price.
* Labour, one line per set rate: cut metres (saw cuts of the given sheet usage + routing length
  of shaped parts; unpriced without sheet usage), applied edge metres, holes, parts.
* `markup = subtotal × markupPercent/100`, `tax = (subtotal + markup) × taxPercent/100`.
* A line without a price is kept as UNPRICED (amount 0, warning `production.quote.unpriced`,
  `Quote::complete == false`); prices are never invented. Currency is a label only.
* `quoteTable`, `quoteText`, `formatMoney` (2 decimals, locale-independent) render it.

Limitations: no per-material waste factors, no rounding of quantities to purchasable units
(by-area quotes price fractional m²), no edge-band roll rounding, no discounts.
