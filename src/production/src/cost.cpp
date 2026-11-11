// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/production/cost.hpp"

#include "collect.hpp"

namespace fos::production {

const char* toString(SheetCountSource s) noexcept {
    switch (s) {
    case SheetCountSource::AreaLowerBound: return "AreaLowerBound";
    case SheetCountSource::Supplied: return "Supplied";
    case SheetCountSource::Unavailable: return "Unavailable";
    }
    return "Unknown";
}

CostEstimate costEstimate(const domain::Model& model, const CostOptions& options, const Scope& scope) {
    CostEstimate out;
    out.currency = options.currency;
    auto take = [&](const core::Diagnostics& ds) {
        for (const auto& d : ds) {
            // The scope diagnostic would otherwise be repeated by each sub-report.
            bool dup = false;
            for (const auto& e : out.diagnostics)
                dup = dup || (e.code == d.code && e.entityId == d.entityId && e.message == d.message);
            if (!dup) out.diagnostics.push_back(d);
        }
    };

    const MaterialReport mats = materialReport(model, scope);
    take(mats.diagnostics);
    for (const auto& m : mats.rows) {
        MaterialCostLine line;
        line.materialId = m.materialId;
        line.name = m.name;
        line.missing = m.missing;
        line.costPerSheet = m.costPerSheet;
        const auto supplied = options.sheetCounts.find(m.materialId);
        if (supplied != options.sheetCounts.end() && supplied->second >= 0) {
            line.sheets = supplied->second;
            line.source = SheetCountSource::Supplied;
            if (m.sheetEstimateAvailable && line.sheets < m.sheetsLowerBoundByArea)
                detail::warn(out.diagnostics, "production.cost.sheet_count_below_bound",
                             "Supplied sheet count " + std::to_string(line.sheets) + " for material '" + m.name +
                                 "' is below the area lower bound of " +
                                 std::to_string(m.sheetsLowerBoundByArea) +
                                 " sheet(s); the parts cannot fit on that many sheets.",
                             m.materialId.str());
        } else if (m.sheetEstimateAvailable) {
            line.sheets = m.sheetsLowerBoundByArea;
            line.source = SheetCountSource::AreaLowerBound;
            out.materialsAtAreaLowerBound = true;
        }
        line.costKnown = !m.missing && line.source != SheetCountSource::Unavailable &&
                         detail::positiveFinite(m.costPerSheet);
        if (line.costKnown)
            line.cost = line.sheets * m.costPerSheet;
        else
            out.complete = false;
        out.materialsTotal += line.cost;
        out.materials.push_back(std::move(line));
    }

    const EdgeBandingReport bands = edgeBandingReport(model, options.edgeBanding, scope);
    take(bands.diagnostics);
    for (const auto& b : bands.rows) {
        out.edgeBanding.push_back(
            {b.edgeBandId, b.name, b.missing, b.purchaseLengthM, b.costPerMeter, b.costKnown, b.cost});
        if (!b.costKnown) out.complete = false;
        out.edgeBandingTotal += b.cost;
    }

    const HardwareReport hw = hardwareReport(model, scope);
    take(hw.diagnostics);
    for (const auto& h : hw.rows) {
        out.hardware.push_back(
            {h.definitionId, h.name, h.unknownDefinition, h.quantity, h.unitCost, h.costKnown, h.totalCost});
        if (!h.costKnown) out.complete = false;
        out.hardwareTotal += h.totalCost;
    }

    out.total = out.materialsTotal + out.edgeBandingTotal + out.hardwareTotal;
    return out;
}

} // namespace fos::production
