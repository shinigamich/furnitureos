// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/app/quote_rows.hpp"

#include "fos/project/outputs.hpp"

#include <set>

namespace fos::app {
namespace {

std::string s(const QString& text) { return text.toStdString(); }

/// Up to 3 decimals, trailing zeros removed, locale-independent ("1.5", "12").
std::string quantityText(double v) {
    QString t = QString::number(v, 'f', 3);
    while (t.contains(QLatin1Char('.')) && (t.endsWith(QLatin1Char('0')) || t.endsWith(QLatin1Char('.')))) t.chop(1);
    if (t == QStringLiteral("-0")) t = QStringLiteral("0");
    return t.toStdString();
}

} // namespace

PricingInput pricingInput(const project::Project& project) {
    PricingInput in;
    if (const auto& p = project.settings.pricing) {
        in.wastePercent = p->wastePercent;
        in.markupPercent = p->markupPercent;
        in.taxPercent = p->taxPercent;
        in.labour = p->labour;
    }
    return in;
}

project::Command pricingSettingsCommand(const PricingInput& input) {
    project::Command c;
    c.label = project::CommandLabel{"Change pricing settings", {}};
    c.apply = [input](project::Project& p) -> core::Result<project::Affected> {
        production::PricingSettings settings;
        settings.currency = p.metadata.currency; // not stored in the file (see ProjectSettings::pricing)
        settings.wastePercent = input.wastePercent;
        settings.markupPercent = input.markupPercent;
        settings.taxPercent = input.taxPercent;
        settings.labour = input.labour;
        if (auto ok = production::checkPricingSettings(settings); !ok) return ok.error();
        settings.currency.clear();
        p.settings.pricing = settings;
        project::Affected a;
        a.settings = true;
        return a;
    };
    return c;
}

project::Command resetPricingSettingsCommand() {
    project::Command c;
    c.label = project::CommandLabel{"Reset pricing settings", {}};
    c.apply = [](project::Project& p) -> core::Result<project::Affected> {
        p.settings.pricing.reset();
        project::Affected a;
        a.settings = true;
        return a;
    };
    return c;
}

std::map<domain::MaterialId, production::SheetUsage> sheetUsageFromPlans(const project::CuttingPlans& plans,
                                                                         core::Diagnostics& diagnostics) {
    return project::quoteSheetUsage(plans, diagnostics);
}

core::Result<production::Quote> projectQuote(const project::Project& project, const domain::Model& model,
                                             const project::CuttingPlans* plans) {
    production::PricingSettings settings = project.settings.pricing.value_or(production::PricingSettings{});
    settings.currency = project.metadata.currency;
    production::QuoteOptions options;
    options.edgeBanding = project.settings.edgeBanding;
    core::Diagnostics extra;
    if (plans) {
        options.sheetUsage = sheetUsageFromPlans(*plans, extra);
        for (const auto& d : plans->diagnostics)
            if (d.severity != core::Severity::Info) extra.push_back(d);
    }
    auto q = production::quote(model, settings, options);
    if (!q) return q.error();
    production::Quote result = std::move(q.value());
    result.diagnostics.insert(result.diagnostics.begin(), extra.begin(), extra.end());
    return result;
}

QString QuoteText::category(production::QuoteCategory category) {
    switch (category) {
    case production::QuoteCategory::Material: return tr("Material");
    case production::QuoteCategory::EdgeBand: return tr("Edge band");
    case production::QuoteCategory::Hardware: return tr("Hardware");
    case production::QuoteCategory::Labour: return tr("Labour");
    }
    return {};
}

QString QuoteText::incompleteBanner(const production::Quote& quote) {
    if (quote.complete) return {};
    return tr("INCOMPLETE: %n item(s) have no price and are NOT included in the total. "
              "Enter the missing prices in the project libraries or pricing settings.",
              nullptr, quote.unpricedCount);
}

QuoteRows QuoteText::rows(const production::Quote& q) {
    QuoteRows r;
    r.table.header = {s(tr("Category")), s(tr("Item")),   s(tr("Quantity")), s(tr("Unit")),
                      s(tr("Unit price")), s(tr("Amount")), s(tr("Note"))};
    for (const auto& l : q.lines) {
        r.table.rows.push_back({s(category(l.category)), l.description, quantityText(l.quantity), l.unit,
                                l.priced() ? production::formatMoney(*l.unitPrice) : s(tr("UNPRICED")),
                                production::formatMoney(l.amount), l.note});
        r.unpriced.push_back(!l.priced());
    }
    r.summaryStart = r.table.rows.size();
    auto total = [&](const QString& label, double amount, const QString& note = {}) {
        r.table.rows.push_back({"", s(label), "", "", "", production::formatMoney(amount), s(note)});
        r.unpriced.push_back(false);
    };
    total(tr("Materials subtotal"), q.materialsSubtotal);
    total(tr("Edge banding subtotal"), q.edgeBandsSubtotal);
    total(tr("Hardware subtotal"), q.hardwareSubtotal);
    total(tr("Labour subtotal"), q.labourSubtotal);
    total(tr("Subtotal"), q.subtotal,
          q.complete ? QString() : tr("%n unpriced line(s) not included", nullptr, q.unpricedCount));
    total(tr("Markup %1 %").arg(QString::fromStdString(quantityText(q.markupPercent))), q.markup);
    total(tr("Tax %1 %").arg(QString::fromStdString(quantityText(q.taxPercent))), q.tax);
    total(q.currency.empty() ? tr("TOTAL") : tr("TOTAL (%1)").arg(QString::fromStdString(q.currency)), q.total);
    return r;
}

} // namespace fos::app
