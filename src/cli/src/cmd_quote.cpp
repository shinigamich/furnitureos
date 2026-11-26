// SPDX-License-Identifier: AGPL-3.0-only
// fos quote  (customer quote, fos/production/quote.hpp) + helpers shared with `fos batch quote`
#include "common.hpp"

#include "fos/formats/json.hpp"
#include "fos/production/csv.hpp"
#include "fos/production/quote.hpp"
#include "fos/project/outputs.hpp"


namespace fos::cli::detail {
namespace {

const char* kQuoteHelp =
    "Usage: fos quote <file.fosp> [--format text|csv|json] [--sheets]\n\n"
    "Customer quote of the resolved model: materials, edge banding, hardware, optional labour,\n"
    "markup and tax. Prices come only from the project libraries (material costPerM2 / costPerSheet,\n"
    "edge band costPerMeter, hardware unitCost) and the pricing settings (settings.pricing: waste,\n"
    "labour rates, markup and tax percentages). Currency = project currency (label only, no conversion).\n"
    "Without --sheets materials are priced by cut area plus the waste percentage. With --sheets the\n"
    "cutting plans are optimised (heuristic, as 'fos cut') and materials are priced per sheet used;\n"
    "the cut length of the plans feeds the per-cut-metre labour rate.\n"
    "Items without a price are listed as UNPRICED and are NOT in the total (no price is invented).\n"
    "Exit code 1 when the quote is incomplete (unpriced lines) or the project has model errors.\n";

std::string jsonNumberText(double v) { return production::formatMoney(v); }

} // namespace

std::string quoteJson(const production::Quote& q) {
    using formats::Json;
    Json j = Json::object();
    j["currency"] = q.currency;
    j["bySheets"] = q.bySheets;
    j["complete"] = q.complete;
    j["unpricedCount"] = q.unpricedCount;
    Json lines = Json::array();
    for (const auto& l : q.lines) {
        Json o = Json::object();
        o["category"] = production::toString(l.category);
        o["id"] = l.id;
        o["description"] = l.description;
        o["quantity"] = l.quantity;
        o["unit"] = l.unit;
        o["priced"] = l.priced();
        o["unitPrice"] = l.priced() ? Json(*l.unitPrice) : Json(nullptr);
        o["amount"] = l.amount;
        o["note"] = l.note;
        lines.push_back(std::move(o));
    }
    j["lines"] = std::move(lines);
    // Money totals as 2-decimal strings: exact for display / accounting, no binary float noise.
    j["materialsSubtotal"] = jsonNumberText(q.materialsSubtotal);
    j["boughtInSubtotal"] = jsonNumberText(q.boughtInSubtotal);
    j["edgeBandsSubtotal"] = jsonNumberText(q.edgeBandsSubtotal);
    j["hardwareSubtotal"] = jsonNumberText(q.hardwareSubtotal);
    j["labourSubtotal"] = jsonNumberText(q.labourSubtotal);
    j["subtotal"] = jsonNumberText(q.subtotal);
    j["markupPercent"] = q.markupPercent;
    j["markup"] = jsonNumberText(q.markup);
    j["taxPercent"] = q.taxPercent;
    j["tax"] = jsonNumberText(q.tax);
    j["total"] = jsonNumberText(q.total);
    Json diags = Json::array();
    for (const auto& d : q.diagnostics)
        diags.push_back({{"severity", severityText(d.severity)}, {"code", d.code}, {"entityId", d.entityId},
                         {"message", d.message}});
    j["diagnostics"] = std::move(diags);
    return j.dump(2, ' ', false, Json::error_handler_t::replace) + "\n";
}

std::optional<std::string> quoteFormat(const ParsedArgs& a) {
    const std::string f = a.value("format").value_or("text");
    if (f != "text" && f != "csv" && f != "json") return std::nullopt;
    return f;
}

int printQuote(Io& io, const production::Quote& q, const std::string& format, const core::Diagnostics& extra,
               const std::string& header) {
    core::Diagnostics all = extra;
    all.insert(all.end(), q.diagnostics.begin(), q.diagnostics.end());
    if (format == "json") {
        production::Quote copy = q;
        copy.diagnostics = all;
        io.out << quoteJson(copy);
        io.err << header;
    } else if (format == "csv") {
        io.out << production::toCsv(production::quoteTable(q));
        io.err << header;
        if (!all.empty()) printDiagnostics(io.err, all);
    } else {
        io.out << header << production::quoteText(q);
        if (!all.empty()) {
            io.out << "Diagnostics:\n";
            printDiagnostics(io.out, all);
        }
    }
    return q.complete && !core::hasErrors(all) ? kExitOk : kExitValidationErrors;
}

int cmdQuote(const std::vector<std::string>& args, Io& io) {
    auto parsed = parseArgs(args, {{"format"}, {"sheets", false}});
    if (!parsed) return usageError(io, "quote", parsed.error().message);
    const ParsedArgs& a = parsed.value();
    if (a.help) {
        io.out << kQuoteHelp;
        return kExitOk;
    }
    if (a.positional.size() != 1) return usageError(io, "quote", "expected exactly one project file.");
    const auto format = quoteFormat(a);
    if (!format) return usageError(io, "quote", "--format must be text, csv or json.");
    auto loaded = loadProjectOrReport(io, "quote", a.positional[0]);
    if (!loaded) return kExitUsageOrIo;
    const auto& project = loaded->project;
    production::PricingSettings settings = project.settings.pricing.value_or(production::PricingSettings{});
    settings.currency = project.metadata.currency;
    production::QuoteOptions options;
    options.edgeBanding = project.settings.edgeBanding;
    core::Diagnostics extra;
    if (a.has("sheets")) {
        const auto plans = project::cuttingPlans(loaded->resolved, project.cutting);
        options.sheetUsage = project::quoteSheetUsage(plans, extra);
        for (const auto& d : plans.diagnostics)
            if (d.severity != core::Severity::Info) extra.push_back(d);
    }
    auto q = production::quote(loaded->resolved.model, settings, options);
    if (!q) {
        io.err << "fos quote: " << q.error().message << "\n";
        return kExitUsageOrIo;
    }
    const std::string incomplete = resolveErrorNotice("quote", *loaded);
    const int rc = printQuote(io, q.value(), *format, extra, "");
    io.err << incomplete;
    return incomplete.empty() ? rc : kExitValidationErrors;
}

} // namespace fos::cli::detail
