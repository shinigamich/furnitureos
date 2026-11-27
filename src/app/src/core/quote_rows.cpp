// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/app/quote_rows.hpp"

#include "fos/project/outputs.hpp"

#include <set>
#include <utility>

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

/// Display text of the production module's unit codes and fixed notes (other notes, which
/// carry numbers, stay as the engine wrote them).
std::string unitText(const std::string& unit) {
    static const std::pair<const char*, const char*> units[] = {
        {"sheet", QT_TRANSLATE_NOOP("QuoteText", "sheet")}, {"m2", QT_TRANSLATE_NOOP("QuoteText", "m²")},
        {"m", QT_TRANSLATE_NOOP("QuoteText", "m")},         {"pc", QT_TRANSLATE_NOOP("QuoteText", "pc")},
        {"hole", QT_TRANSLATE_NOOP("QuoteText", "hole")},   {"part", QT_TRANSLATE_NOOP("QuoteText", "part")}};
    for (const auto& [code, text] : units)
        if (unit == code) return s(QuoteText::tr(text));
    return unit;
}

std::string noteText(const std::string& note) {
    static const char* const notes[] = {
        QT_TRANSLATE_NOOP("QuoteText", "material is not in the libraries"),
        QT_TRANSLATE_NOOP("QuoteText", "sheets from the cutting plan"),
        QT_TRANSLATE_NOOP("QuoteText", "no price per sheet or per m2 in the library"),
        QT_TRANSLATE_NOOP("QuoteText", "no price per m2 or per sheet in the library"),
        QT_TRANSLATE_NOOP("QuoteText", "edge band is not in the libraries"),
        QT_TRANSLATE_NOOP("QuoteText", "no price per metre in the library"),
        QT_TRANSLATE_NOOP("QuoteText", "hardware definition is not in the catalogue"),
        QT_TRANSLATE_NOOP("QuoteText", "no price per metre in the catalogue"),
        QT_TRANSLATE_NOOP("QuoteText", "no unit price in the catalogue"),
        QT_TRANSLATE_NOOP("QuoteText", "applied length")};
    for (const char* text : notes)
        if (note == text) return s(QuoteText::tr(text));
    return note;
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
    case production::QuoteCategory::BoughtIn: return tr("Bought-in");
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
        r.table.rows.push_back({s(category(l.category)), l.description, quantityText(l.quantity), unitText(l.unit),
                                l.priced() ? production::formatMoney(*l.unitPrice) : s(tr("UNPRICED")),
                                production::formatMoney(l.amount), noteText(l.note)});
        r.unpriced.push_back(!l.priced());
    }
    r.summaryStart = r.table.rows.size();
    auto total = [&](const QString& label, double amount, const QString& note = {}) {
        r.table.rows.push_back({"", s(label), "", "", "", production::formatMoney(amount), s(note)});
        r.unpriced.push_back(false);
    };
    total(tr("Materials subtotal"), q.materialsSubtotal);
    bool boughtIn = false;
    for (const auto& l : q.lines) boughtIn = boughtIn || l.category == production::QuoteCategory::BoughtIn;
    if (boughtIn) total(tr("Bought-in items subtotal"), q.boughtInSubtotal);
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
