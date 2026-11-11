// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/production/reports.hpp"

#include "collect.hpp"
#include "fos/core/units.hpp"

#include <algorithm>
#include <map>

namespace fos::production {
namespace {

/// Appends the entries of `extra` (already sorted by id, being a std::map) after `rows`.
template <class Row, class Id> void appendMissing(std::vector<Row>& rows, std::map<Id, Row>& extra) {
    for (auto& [id, row] : extra) rows.push_back(std::move(row));
}

/// Smallest whole number of sheets whose area covers `cutArea` (both m2, sheet > 0).
int sheetsByArea(double cutArea, double sheetArea) {
    if (cutArea <= 0.0) return 0;
    const double n = std::ceil(cutArea / sheetArea - 1e-9); // tolerate rounding noise at exact multiples
    return n >= 2.0e9 ? 2000000000 : std::max(1, static_cast<int>(n));
}

} // namespace

// ---------------------------------------------------------------- materials

MaterialReport materialReport(const domain::Model& model, const Scope& scope) {
    const PartList parts = partList(model, scope);
    MaterialReport out;
    for (const auto& d : parts.diagnostics)
        if (d.code != "production.edgeband.missing") out.diagnostics.push_back(d);

    std::map<domain::MaterialId, MaterialRow> acc;
    for (const auto& r : parts.rows) {
        auto& row = acc[r.materialId];
        row.materialId = r.materialId;
        ++row.partCount;
        row.finishedAreaM2 += r.finishedAreaM2();
        row.cutAreaM2 += r.cutAreaM2();
    }
    for (const auto& m : model.materials) {
        auto it = acc.find(m.id);
        if (it == acc.end()) continue;
        MaterialRow row = std::move(it->second);
        acc.erase(it); // a duplicated material id in the model is reported once
        row.name = m.name;
        row.thickness = m.thickness;
        row.sheetLength = m.sheetLength;
        row.sheetWidth = m.sheetWidth;
        row.costPerSheet = m.costPerSheet;
        row.sheetAreaM2 = detail::areaM2(m.sheetLength, m.sheetWidth);
        row.sheetEstimateAvailable = row.sheetAreaM2 > 0.0;
        if (row.sheetEstimateAvailable) {
            row.sheetsLowerBoundByArea = sheetsByArea(row.cutAreaM2, row.sheetAreaM2);
            row.costKnown = detail::positiveFinite(m.costPerSheet);
            if (row.costKnown) row.costAtAreaLowerBound = row.sheetsLowerBoundByArea * m.costPerSheet;
        } else {
            detail::warn(out.diagnostics, "production.material.invalid_sheet",
                         "Material '" + m.name + "' (" + m.id.str() + ") has sheet size " +
                             core::formatLength(m.sheetLength) + " x " + core::formatLength(m.sheetWidth) +
                             " mm; no sheet estimate can be given.",
                         m.id.str());
        }
        out.rows.push_back(std::move(row));
    }
    for (auto& [id, row] : acc) row.missing = true;
    appendMissing(out.rows, acc);
    return out;
}

// ------------------------------------------------------------- edge banding

EdgeBandingReport edgeBandingReport(const domain::Model& model, const EdgeBandingOptions& options,
                                    const Scope& scope) {
    const auto c = detail::collect(model, scope);
    EdgeBandingReport out;
    for (const auto& d : partList(model, scope).diagnostics)
        if (d.code == "production.edgeband.missing" || d.code == "production.scope.unknown_assembly")
            out.diagnostics.push_back(d);

    const double overhang = detail::positiveFinite(options.overhangPerEnd) ? options.overhangPerEnd : 0.0;
    const double waste = detail::positiveFinite(options.wastePercent) ? options.wastePercent : 0.0;

    std::map<domain::EdgeBandId, EdgeBandingRow> acc;
    for (const auto& ref : c.parts)
        for (auto s : domain::kAllEdges) {
            const auto& e = ref.part->edge(s);
            if (!e) continue;
            auto& row = acc[*e];
            row.edgeBandId = *e;
            ++row.edgeCount;
            const double len = domain::edgeLength(*ref.part, s);
            if (detail::positiveFinite(len)) row.appliedLengthM += core::mmToM(len);
        }
    auto finish = [&](EdgeBandingRow& row) {
        row.overhangAllowanceM = core::mmToM(row.edgeCount * 2.0 * overhang);
        row.wasteAllowanceM = (row.appliedLengthM + row.overhangAllowanceM) * waste / 100.0;
        row.purchaseLengthM = row.appliedLengthM + row.overhangAllowanceM + row.wasteAllowanceM;
        row.costKnown = !row.missing && detail::positiveFinite(row.costPerMeter);
        if (row.costKnown) row.cost = row.purchaseLengthM * row.costPerMeter;
    };
    for (const auto& eb : model.edgeBands) {
        auto it = acc.find(eb.id);
        if (it == acc.end()) continue;
        EdgeBandingRow row = std::move(it->second);
        acc.erase(it);
        row.name = eb.name;
        row.thickness = eb.thickness;
        row.width = eb.width;
        row.costPerMeter = eb.costPerMeter;
        finish(row);
        out.rows.push_back(std::move(row));
    }
    for (auto& [id, row] : acc) {
        row.missing = true;
        finish(row);
    }
    appendMissing(out.rows, acc);
    return out;
}

// ----------------------------------------------------------------- hardware

HardwareReport hardwareReport(const domain::Model& model, const Scope& scope) {
    auto c = detail::collect(model, scope);
    HardwareReport out;
    out.diagnostics = std::move(c.diagnostics);

    std::map<domain::HardwareId, HardwareRow> acc;
    for (const auto* a : c.assemblies)
        for (const auto& h : a->hardware) {
            if (h.quantity <= 0) {
                detail::warn(out.diagnostics, "production.hardware.invalid_quantity",
                             "Hardware instance '" + h.id.str() + "' in assembly '" + a->name +
                                 "' has quantity " + std::to_string(h.quantity) + "; it is not counted.",
                             h.id.str());
                continue;
            }
            auto& row = acc[h.definition];
            row.definitionId = h.definition;
            ++row.instanceCount;
            row.quantity += h.quantity;
            if (!model.findHardware(h.definition))
                detail::warn(out.diagnostics, "production.hardware.unknown_definition",
                             "Hardware instance '" + h.id.str() + "' in assembly '" + a->name +
                                 "' references definition '" + h.definition.str() +
                                 "' which is not in the hardware catalogue; it has no name or price.",
                             h.id.str());
        }
    for (const auto& def : model.hardwareCatalog) {
        auto it = acc.find(def.id);
        if (it == acc.end()) continue;
        HardwareRow row = std::move(it->second);
        acc.erase(it);
        row.name = def.name;
        row.manufacturer = def.manufacturer;
        row.article = def.article;
        row.kind = def.kind;
        row.dimensionsVerified = def.dimensionsVerified;
        row.unitCost = def.unitCost;
        row.costKnown = detail::positiveFinite(def.unitCost);
        if (row.costKnown) row.totalCost = row.quantity * def.unitCost;
        out.rows.push_back(std::move(row));
    }
    for (auto& [id, row] : acc) row.unknownDefinition = true;
    appendMissing(out.rows, acc);
    return out;
}

// ---------------------------------------------------------------- machining

MachiningSummary machiningSummary(const domain::Model& model, const MachiningOptions& options,
                                  const Scope& scope) {
    auto c = detail::collect(model, scope);
    MachiningSummary out;
    out.diagnostics = std::move(c.diagnostics);
    const double tol = detail::positiveFinite(options.diameterTolerance) ? options.diameterTolerance : 0.0;

    for (const auto& ref : c.parts) {
        const auto& p = *ref.part;
        if (p.operations.empty() && !options.includePartsWithoutOperations) continue;
        MachiningRow row;
        row.partNumber = ref.number;
        row.assemblyId = ref.assembly->id;
        row.assemblyName = ref.assembly->name;
        row.partId = p.id;
        row.partName = p.name;
        for (const auto& op : p.operations) {
            if (const auto* h = std::get_if<domain::Hole>(&op.shape)) {
                ++row.holeCount;
                auto it = std::find_if(row.holes.begin(), row.holes.end(), [&](const HoleGroup& g) {
                    return g.face == h->face && std::fabs(g.diameter - h->diameter) <= tol;
                });
                if (it == row.holes.end()) {
                    row.holes.push_back({h->diameter, h->face, 0, 0});
                    it = std::prev(row.holes.end());
                }
                ++it->count;
                if (h->through) ++it->throughCount;
            } else if (const auto* g = std::get_if<domain::Groove>(&op.shape)) {
                ++row.grooveCount;
                const double len = std::hypot(g->u2 - g->u1, g->v2 - g->v1);
                if (std::isfinite(len)) row.grooveLength += len;
            } else {
                ++row.pocketCount;
            }
        }
        std::stable_sort(row.holes.begin(), row.holes.end(), [](const HoleGroup& a, const HoleGroup& b) {
            const double da = std::isfinite(a.diameter) ? a.diameter : 0.0;
            const double db = std::isfinite(b.diameter) ? b.diameter : 0.0;
            return da != db ? da < db : static_cast<int>(a.face) < static_cast<int>(b.face);
        });
        out.totalHoles += row.holeCount;
        out.totalGrooves += row.grooveCount;
        out.totalPockets += row.pocketCount;
        out.rows.push_back(std::move(row));
    }
    return out;
}

// ------------------------------------------------------------------ summary

ProjectSummary projectSummary(const domain::Model& model, const Scope& scope) {
    const PartList parts = partList(model, scope);
    const auto c = detail::collect(model, scope);
    ProjectSummary out;
    out.assemblyCount = static_cast<int>(c.assemblies.size());
    out.partCount = static_cast<int>(parts.rows.size());

    std::vector<domain::MaterialId> mats;
    std::vector<domain::EdgeBandId> bands;
    for (const auto& r : parts.rows) {
        mats.push_back(r.materialId);
        for (const auto& e : r.edges)
            if (e.id) bands.push_back(*e.id);
        out.finishedAreaM2 += r.finishedAreaM2();
        out.cutAreaM2 += r.cutAreaM2();
        out.operationCount += r.operationCount;
    }
    auto distinct = [](auto& v) {
        std::sort(v.begin(), v.end());
        return static_cast<int>(std::unique(v.begin(), v.end()) - v.begin());
    };
    out.materialCount = distinct(mats);
    out.edgeBandCount = distinct(bands);
    for (const auto& row : edgeBandingReport(model, {}, scope).rows) out.edgeBandingAppliedM += row.appliedLengthM;

    for (const auto* a : c.assemblies) {
        AssemblySummary s;
        s.id = a->id;
        s.name = a->name;
        s.partCount = static_cast<int>(a->parts.size());
        for (const auto& h : a->hardware)
            if (h.quantity > 0) s.hardwareQuantity += h.quantity;
        for (const auto& p : a->parts) {
            s.operationCount += static_cast<int>(p.operations.size());
            s.finishedAreaM2 += detail::areaM2(p.length, p.width);
        }
        const auto size = a->bounds().size();
        if (std::isfinite(size.x) && std::isfinite(size.y) && std::isfinite(size.z)) {
            s.envelopeWidth = size.x;
            s.envelopeDepth = size.y;
            s.envelopeHeight = size.z;
        }
        out.hardwareQuantity += s.hardwareQuantity;
        out.assemblies.push_back(std::move(s));
    }
    return out;
}

} // namespace fos::production
