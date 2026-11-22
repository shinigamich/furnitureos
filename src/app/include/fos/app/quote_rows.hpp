// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Quote view-model and pricing settings editor glue. All prices and totals come from
/// production::quote() (fos/production/quote.hpp); nothing is priced here. This file only
/// converts editor input to ProjectSettings::pricing (as an undoable command), feeds the
/// project's cutting plans to the quote, and lays the result out as translated table rows.
#include "fos/production/quote.hpp"
#include "fos/project/commands.hpp"
#include "fos/project/outputs.hpp"

#include <QCoreApplication>

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace fos::app {

/// What the pricing settings dialog edits (percent values: 10 = 10 %). Unset labour rates
/// produce no labour line. The currency is Metadata::currency, not edited here.
struct PricingInput {
    double wastePercent = 0.0;
    double markupPercent = 0.0;
    double taxPercent = 0.0;
    production::LabourRates labour;
    bool operator==(const PricingInput&) const = default;
};

/// project.settings.pricing as editor input (all zero / unset when pricing is not configured).
PricingInput pricingInput(const project::Project& project);

/// Undoable command setting project.settings.pricing to `input` (label "Change pricing
/// settings", affects settings). Invalid values (production::checkPricingSettings: negative,
/// not finite, percentage > 1000) make the command fail and leave the project unchanged.
/// Every other project setting is kept.
project::Command pricingSettingsCommand(const PricingInput& input);
/// Undoable command removing project.settings.pricing (label "Reset pricing settings"): the
/// key is no longer written to the file.
project::Command resetPricingSettingsCommand();

/// SheetUsage per material from the SAME plans as the Cutting workspace (project::cuttingPlans):
/// guillotine sheets taken from purchased stock (listed remnants are owned, not counted) plus
/// nested sheets; cut length = sum of the guillotine sheets' cut lengths (mm). A material whose
/// guillotine or nesting optimisation failed is left out (it is then priced by area) and an
/// project.quote.plan_failed warning (shared with the CLI via project::quoteSheetUsage) naming it is appended to `diagnostics`.
std::map<domain::MaterialId, production::SheetUsage> sheetUsageFromPlans(const project::CuttingPlans& plans,
                                                                         core::Diagnostics& diagnostics);

/// production::quote() of `model` with the project's pricing (currency = Metadata::currency,
/// edge banding allowances = project settings) and, when `plans` is given, the sheet usage of
/// those plans. Plan diagnostics above Info and plan failures are appended to
/// Quote::diagnostics. Error = the production module's message (invalid settings).
core::Result<production::Quote> projectQuote(const project::Project& project, const domain::Model& model,
                                             const project::CuttingPlans* plans);

/// Display rows of a quote: one row per line, then the category subtotals, subtotal, markup,
/// tax and total rows. Captions are in the UI language; money uses production::formatMoney.
struct QuoteRows {
    production::Table table;        ///< Category, Item, Quantity, Unit, Unit price, Amount, Note.
    std::vector<bool> unpriced;     ///< Per table row: true for an UNPRICED line.
    std::size_t summaryStart = 0;   ///< Index of the first summary (subtotal ...) row.
};

class QuoteText {
    Q_DECLARE_TR_FUNCTIONS(QuoteText)
public:
    static QString category(production::QuoteCategory category);
    /// "INCOMPLETE: n item(s) have no price ..." or empty when the quote is complete.
    static QString incompleteBanner(const production::Quote& quote);
    static QuoteRows rows(const production::Quote& quote);
};

} // namespace fos::app
