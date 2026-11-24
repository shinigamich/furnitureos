// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/production/quote.hpp"

#include "collect.hpp"

#include "fos/core/units.hpp"

#include <cmath>

namespace fos::production {
namespace {

constexpr double kMaxPercent = 1000.0;

bool finiteNonNegative(double v) noexcept { return std::isfinite(v) && v >= 0.0; }

core::Result<void> checkValue(const char* field, double v, bool percent) {
    if (!finiteNonNegative(v) || (percent && v > kMaxPercent))
        return core::makeError(core::ErrorCode::InvalidArgument,
                               std::string("Pricing setting '") + field + "' is " + core::formatLength(v, 6) +
                                   (percent ? "; it must be a percentage from 0 to 1000."
                                            : "; it must be a finite number >= 0."));
    return {};
}

struct Builder {
    Quote q;

    void take(const core::Diagnostics& ds) {
        for (const auto& d : ds) {
            bool dup = false;
            for (const auto& e : q.diagnostics)
                dup = dup || (e.code == d.code && e.entityId == d.entityId && e.message == d.message);
            if (!dup) q.diagnostics.push_back(d);
        }
    }

    void add(QuoteLine line, double& subtotal) {
        if (line.priced()) {
            line.amount = line.quantity * *line.unitPrice;
            subtotal += line.amount;
        } else {
            line.amount = 0;
            ++q.unpricedCount;
            q.complete = false;
            detail::warn(q.diagnostics, "production.quote.unpriced",
                         std::string(toString(line.category)) + " '" + line.description + "' (" +
                             core::formatLength(line.quantity, 3) + " " + line.unit +
                             ") has no price and is listed as unpriced; the quote total does not include it" +
                             (line.note.empty() ? "." : " (" + line.note + ")."),
                         line.id);
        }
        q.lines.push_back(std::move(line));
    }
};

QuoteLine materialLine(const domain::Model& model, const MaterialRow& row, const PricingSettings& settings,
                       const QuoteOptions& options, bool& bySheets) {
    QuoteLine line;
    line.category = QuoteCategory::Material;
    line.id = row.materialId.str();
    line.description = row.missing ? row.materialId.str() : row.name;
    const domain::Material* mat = row.missing ? nullptr : model.findMaterial(row.materialId);
    const double perM2 = mat != nullptr ? mat->costPerSquareMeter : 0.0;
    const double perSheet = mat != nullptr ? mat->costPerSheet : 0.0;
    const auto usage = options.sheetUsage.find(row.materialId);
    if (usage != options.sheetUsage.end()) {
        bySheets = true;
        line.quantity = usage->second.sheets;
        line.unit = "sheet";
        if (mat == nullptr) {
            line.note = "material is not in the libraries";
        } else if (detail::positiveFinite(perSheet)) {
            line.unitPrice = perSheet;
            line.note = "sheets from the cutting plan";
        } else if (detail::positiveFinite(perM2) && row.sheetAreaM2 > 0) {
            line.unitPrice = perM2 * row.sheetAreaM2;
            line.note = "sheets from the cutting plan; sheet price = price per m2 x sheet area " +
                        core::formatLength(row.sheetAreaM2, 3) + " m2";
        } else {
            line.note = "no price per sheet or per m2 in the library";
        }
        return line;
    }
    line.quantity = row.cutAreaM2 * (1.0 + settings.wastePercent / 100.0);
    line.unit = "m2";
    const std::string area = "cut area " + core::formatLength(row.cutAreaM2, 3) + " m2 + " +
                             core::formatLength(settings.wastePercent, 3) + " % waste";
    if (mat == nullptr) {
        line.note = "material is not in the libraries";
    } else if (detail::positiveFinite(perM2)) {
        line.unitPrice = perM2;
        line.note = area;
    } else if (detail::positiveFinite(perSheet) && row.sheetAreaM2 > 0) {
        line.unitPrice = perSheet / row.sheetAreaM2;
        line.note = area + "; price per m2 = sheet price / sheet area " + core::formatLength(row.sheetAreaM2, 3) +
                    " m2";
    } else {
        line.note = "no price per m2 or per sheet in the library";
    }
    return line;
}

QuoteLine labourLine(const char* id, const char* description, double quantity, const char* unit, double rate,
                     std::string note) {
    QuoteLine line;
    line.category = QuoteCategory::Labour;
    line.id = id;
    line.description = description;
    line.quantity = quantity;
    line.unit = unit;
    line.unitPrice = rate;
    line.note = std::move(note);
    return line;
}

} // namespace

const char* toString(QuoteCategory c) noexcept {
    switch (c) {
    case QuoteCategory::Material: return "Material";
    case QuoteCategory::EdgeBand: return "Edge band";
    case QuoteCategory::Hardware: return "Hardware";
    case QuoteCategory::Labour: return "Labour";
    }
    return "Unknown";
}

core::Result<void> checkPricingSettings(const PricingSettings& s) {
    for (const auto& [field, v] : {std::pair{"wastePercent", s.wastePercent}, std::pair{"markupPercent", s.markupPercent},
                                   std::pair{"taxPercent", s.taxPercent}})
        if (auto r = checkValue(field, v, true); !r) return r;
    for (const auto& [field, v] : {std::pair{"labour.perCutMetre", s.labour.perCutMetre},
                                   std::pair{"labour.perEdgeMetre", s.labour.perEdgeMetre},
                                   std::pair{"labour.perHole", s.labour.perHole},
                                   std::pair{"labour.perPart", s.labour.perPart}})
        if (v) {
            if (auto r = checkValue(field, *v, false); !r) return r;
        }
    return {};
}

core::Result<Quote> quote(const domain::Model& model, const PricingSettings& settings, const QuoteOptions& options) {
    if (auto r = checkPricingSettings(settings); !r) return r.error();
    for (const auto& [id, u] : options.sheetUsage)
        if (u.sheets < 0 || !finiteNonNegative(u.cutLengthMm))
            return core::makeError(core::ErrorCode::InvalidArgument,
                                   "Sheet usage of material '" + id.str() + "' is " + std::to_string(u.sheets) +
                                       " sheet(s) with " + core::formatLength(u.cutLengthMm, 3) +
                                       " mm of cuts; both must be finite and >= 0.");
    Builder b;
    b.q.currency = settings.currency;
    b.q.markupPercent = settings.markupPercent;
    b.q.taxPercent = settings.taxPercent;

    const MaterialReport mats = materialReport(model, options.scope);
    b.take(mats.diagnostics);
    for (const auto& row : mats.rows) b.add(materialLine(model, row, settings, options, b.q.bySheets), b.q.materialsSubtotal);
    for (const auto& [id, u] : options.sheetUsage) {
        bool used = false;
        for (const auto& row : mats.rows) used = used || row.materialId == id;
        if (!used)
            detail::warn(b.q.diagnostics, "production.quote.sheet_usage_unused",
                         "Sheet usage was given for material '" + id.str() + "' (" + std::to_string(u.sheets) +
                             " sheet(s)) but no part in the quote uses it; it is not priced.",
                         id.str());
    }

    const EdgeBandingReport bands = edgeBandingReport(model, options.edgeBanding, options.scope);
    b.take(bands.diagnostics);
    double appliedM = 0;
    for (const auto& row : bands.rows) {
        appliedM += row.appliedLengthM;
        QuoteLine line;
        line.category = QuoteCategory::EdgeBand;
        line.id = row.edgeBandId.str();
        line.description = row.missing ? row.edgeBandId.str() : row.name;
        line.quantity = row.purchaseLengthM;
        line.unit = "m";
        if (row.missing)
            line.note = "edge band is not in the libraries";
        else if (row.costKnown)
            line.unitPrice = row.costPerMeter;
        else
            line.note = "no price per metre in the library";
        if (line.priced())
            line.note = "applied " + core::formatLength(row.appliedLengthM, 3) + " m + overhang and waste allowances";
        b.add(std::move(line), b.q.edgeBandsSubtotal);
    }

    const HardwareReport hw = hardwareReport(model, options.scope);
    b.take(hw.diagnostics);
    for (const auto& row : hw.rows) {
        QuoteLine line;
        line.category = QuoteCategory::Hardware;
        line.id = row.definitionId.str();
        line.description = row.unknownDefinition ? row.definitionId.str() : row.name;
        line.quantity = row.quantity;
        line.unit = "pc";
        if (row.perMetre) {
            line.quantity = row.totalLengthM;
            line.unit = "m";
        }
        const std::string pieces = std::to_string(row.linearPieces) + " pc, total length " +
                                   core::formatLength(row.totalLengthM, 3) + " m";
        if (row.unknownDefinition)
            line.note = "hardware definition is not in the catalogue";
        else if (row.costKnown)
            line.unitPrice = row.unitCost;
        else if (row.perMetre && detail::positiveFinite(row.unitCost))
            line.note = "sold per metre but " + std::to_string(row.quantity - row.linearPieces) + " of " +
                        std::to_string(row.quantity) + " pieces have no length";
        else
            line.note = row.perMetre ? "no price per metre in the catalogue" : "no unit price in the catalogue";
        if (row.linearPieces > 0) line.note = line.note.empty() ? pieces : pieces + "; " + line.note;
        b.add(std::move(line), b.q.hardwareSubtotal);
    }

    const LabourRates& rates = settings.labour;
    if (rates.perCutMetre || rates.perHole) {
        const MachiningSummary mach = machiningSummary(model, {}, options.scope);
        b.take(mach.diagnostics);
        if (rates.perCutMetre) {
            const double routingM = mach.totalRoutingLength / 1000.0;
            if (options.sheetUsage.empty()) {
                QuoteLine line = labourLine("labour.cut", "Cutting", routingM, "m", 0, "");
                line.unitPrice.reset();
                line.note = "panel-saw cut length is only known from a cutting plan (quote by sheets)";
                b.add(std::move(line), b.q.labourSubtotal);
            } else {
                double sawM = 0;
                for (const auto& [id, u] : options.sheetUsage) sawM += u.cutLengthMm / 1000.0;
                b.add(labourLine("labour.cut", "Cutting", sawM + routingM, "m", *rates.perCutMetre,
                                 "saw cuts " + core::formatLength(sawM, 3) + " m (cutting plan) + routing " +
                                     core::formatLength(routingM, 3) + " m (shaped parts)"),
                      b.q.labourSubtotal);
            }
        }
        if (rates.perHole)
            b.add(labourLine("labour.hole", "Drilling", mach.totalHoles, "hole", *rates.perHole, ""),
                  b.q.labourSubtotal);
    }
    if (rates.perEdgeMetre)
        b.add(labourLine("labour.edge", "Edge banding", appliedM, "m", *rates.perEdgeMetre, "applied length"),
              b.q.labourSubtotal);
    if (rates.perPart) {
        const ProjectSummary sum = projectSummary(model, options.scope);
        b.add(labourLine("labour.part", "Part handling", sum.partCount, "part", *rates.perPart, ""),
              b.q.labourSubtotal);
    }

    Quote& q = b.q;
    q.subtotal = q.materialsSubtotal + q.edgeBandsSubtotal + q.hardwareSubtotal + q.labourSubtotal;
    q.markup = q.subtotal * settings.markupPercent / 100.0;
    q.tax = (q.subtotal + q.markup) * settings.taxPercent / 100.0;
    q.total = q.subtotal + q.markup + q.tax;
    return std::move(b.q);
}

} // namespace fos::production
