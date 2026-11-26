// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Customer QUOTE (price estimate) of a model: materials, edge banding, hardware and optional
/// labour, then markup and tax. Prices come ONLY from the libraries in the model
/// (domain::Material::costPerSquareMeter / costPerSheet, domain::EdgeBand::costPerMeter,
/// domain::HardwareDefinition::unitCost; 0 = unknown) and from PricingSettings. A line whose
/// price is unknown is kept as an UNPRICED line (amount 0, production.quote.unpriced warning);
/// no price is ever invented, and Quote::complete is then false.
///
/// Currency is a label only: no conversion is performed. Quantities: m2, sheets, metres, pieces.
#include "fos/core/result.hpp"
#include "fos/production/bought_in.hpp"
#include "fos/production/reports.hpp"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace fos::production {

/// Labour rates in the currency of the quote. Unset rates produce no labour line.
struct LabourRates {
    std::optional<double> perCutMetre;  ///< Panel saw cuts (cutting plan) + CNC routing of shaped parts.
    std::optional<double> perEdgeMetre; ///< Applied edge banding length (not the purchase length).
    std::optional<double> perHole;      ///< Drilled holes (Part::operations).
    std::optional<double> perPart;      ///< Handling per part.
    bool operator==(const LabourRates&) const = default;
};

/// Project-level pricing settings. All percentages are percent (10 = 10 %), each >= 0 and finite.
struct PricingSettings {
    std::string currency;      ///< Label only (e.g. "EUR"); no conversion.
    /// Extra board added to the cut area of materials priced BY AREA (offcuts, kerf). Not applied
    /// when sheets come from a cutting plan (whole sheets already include the waste) nor to edge
    /// banding (that has its own allowances in EdgeBandingOptions).
    double wastePercent = 0.0;
    LabourRates labour;
    double markupPercent = 0.0; ///< Applied to the subtotal.
    double taxPercent = 0.0;    ///< Applied to subtotal + markup.
    bool operator==(const PricingSettings&) const = default;
};

/// InvalidArgument naming the field and value when a percentage or rate is negative or not
/// finite, or a percentage exceeds 1000.
core::Result<void> checkPricingSettings(const PricingSettings& settings);

/// Real sheet usage of one material, e.g. from a cutting plan.
struct SheetUsage {
    int sheets = 0;          ///< Full (purchased) sheets; remnants excluded. >= 0.
    double cutLengthMm = 0;  ///< Total saw cut length on those sheets, mm (0 = none / unknown).
};

struct QuoteOptions {
    Scope scope;
    EdgeBandingOptions edgeBanding; ///< Purchase length allowances of the edge band lines.
    /// When non-empty, the quote is "by sheets": each listed material is priced per sheet and the
    /// cut length is the sum of cutLengthMm. Materials not listed are priced by area.
    std::map<domain::MaterialId, SheetUsage> sheetUsage;
};

enum class QuoteCategory { Material, EdgeBand, Hardware, Labour, BoughtIn };
const char* toString(QuoteCategory c) noexcept;

struct QuoteLine {
    QuoteCategory category = QuoteCategory::Material;
    std::string id;          ///< Material / edge band / hardware id; "labour.cut", "labour.edge", "labour.hole", "labour.part".
    std::string description; ///< Library name (raw id when missing) or labour description.
    double quantity = 0;     ///< In `unit`.
    std::string unit;        ///< "m2", "sheet", "m", "pc", "hole", "part".
    std::optional<double> unitPrice; ///< Empty = unpriced.
    double amount = 0;       ///< quantity * unitPrice, 0 when unpriced.
    std::string note;        ///< How the quantity / price was obtained, or why it is unpriced.
    bool priced() const noexcept { return unitPrice.has_value(); }
};

struct Quote {
    std::string currency;
    std::vector<QuoteLine> lines; ///< Materials (model order), bought-in items, edge bands, hardware, labour.
    double materialsSubtotal = 0, edgeBandsSubtotal = 0, hardwareSubtotal = 0, labourSubtotal = 0;
    double boughtInSubtotal = 0; ///< Bought-in items (glass panes).
    double subtotal = 0;
    double markupPercent = 0, markup = 0; ///< markup = subtotal * markupPercent / 100.
    double taxPercent = 0, tax = 0;       ///< tax = (subtotal + markup) * taxPercent / 100.
    double total = 0;                     ///< subtotal + markup + tax.
    bool bySheets = false;    ///< At least one material priced from SheetUsage.
    int unpricedCount = 0;
    bool complete = true;     ///< False when any line is unpriced: the totals are then too low.
    /// Report diagnostics (missing references, ...) and one production.quote.unpriced warning per
    /// unpriced line; production.quote.sheet_usage_unused (warning) for usage of an unused material.
    core::Diagnostics diagnostics;
};

/// Builds the quote of `model` (whose libraries carry the prices). Material lines:
///  * by sheets (material in options.sheetUsage): quantity = sheets, price = costPerSheet, else
///    costPerSquareMeter * sheet area (noted);
///  * by area: quantity = cut area (domain::cutSize blanks) * (1 + wastePercent / 100) m2, price =
///    costPerSquareMeter, else costPerSheet / sheet area (noted).
/// Sheet materials exclude bought-in parts (domain::isBoughtIn). Bought-in lines (category
/// BoughtIn, one per BoughtInRow of boughtInList, in its order): id = material id, description
/// "Glass pane <W> x <H> x <T> mm (<material>)", quantity = total area in m2 (no waste: the
/// supplier cuts to size), price = the material's costPerSquareMeter; unpriced when it is 0 or the
/// material is missing (costPerSheet is never used for glass: no price is invented).
/// Edge band lines: purchase length (EdgeBandingReport) * costPerMeter. Hardware: quantity * unitCost
/// in "pc"; a definition sold per metre (custom "unit" = "m", isPricedPerMetre) has unitCost PER
/// METRE and is quoted as total length (HardwareRow::totalLengthM) in "m" * unitCost, unpriced when
/// any of its instances has no length. Linear lines note the piece count and total length.
/// Labour: one line per set rate. The cut-metre line needs a cutting plan for the panel-saw cuts:
/// without options.sheetUsage it is unpriced (quantity = routing length of shaped parts only).
/// InvalidArgument when checkPricingSettings fails or a SheetUsage value is negative / not finite.
core::Result<Quote> quote(const domain::Model& model, const PricingSettings& settings,
                          const QuoteOptions& options = {});

/// Display table: Category, Item, Quantity, Unit, Unit price, Amount, Note; then the subtotal,
/// markup, tax and total rows. Money is formatted with 2 decimals, quantities with up to 3.
Table quoteTable(const Quote& quote);

/// Human-readable quote: header with the currency, the table, the totals, and an UNPRICED notice
/// when the quote is incomplete.
std::string quoteText(const Quote& quote);

/// Money with exactly 2 decimals, locale-independent ("1234.50").
std::string formatMoney(double value);

} // namespace fos::production
