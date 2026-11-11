// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/production/tables.hpp"

#include "fos/core/units.hpp"

namespace fos::production {
namespace {

using core::formatLength;

std::string joinNotes(const std::vector<std::string>& notes) {
    std::string out;
    for (const auto& s : notes) {
        if (s.empty()) continue;
        if (!out.empty()) out += "; ";
        out += s;
    }
    return out;
}

/// "<id> [MISSING]" for references that cannot be resolved.
std::string flagged(const std::string& id, const std::string& flag) {
    return id.empty() ? "[" + flag + "]" : id + " [" + flag + "]";
}

std::string materialCell(const std::string& name, const domain::MaterialId& id, bool missing, const Terms& t) {
    return missing ? flagged(id.str(), t.missing) : name;
}

std::string edgeCell(const EdgeCell& e, const Terms& t) {
    if (!e.banded()) return {};
    return e.missing ? flagged(e.id->str(), t.missing) : e.name;
}

std::string grainCell(domain::GrainDirection g, const Terms& t) {
    switch (g) {
    case domain::GrainDirection::None: return t.grainNone;
    case domain::GrainDirection::AlongLength: return t.grainAlongLength;
    case domain::GrainDirection::AlongWidth: return t.grainAlongWidth;
    }
    return {};
}

std::string roleCell(domain::PartRole r, const Terms& t) {
    const auto it = t.roles.find(r);
    return it != t.roles.end() ? it->second : std::string(domain::toString(r));
}

std::string money(double v, bool known, const NumberFormat& nf) { return known ? formatLength(v, nf.money) : ""; }

} // namespace

Table partListTable(const PartList& r, const Localisation& loc, const NumberFormat& nf) {
    const auto& h = loc.partList;
    Table t;
    t.header = {h.number, h.assembly, h.partId, h.name, h.role, h.material, h.length, h.width, h.thickness,
                h.cutLength, h.cutWidth, h.grain, h.edgeL1, h.edgeL2, h.edgeW1, h.edgeW2, h.operations};
    for (const auto& p : r.rows)
        t.rows.push_back({std::to_string(p.number), p.assemblyName, p.partId.str(), p.name,
                          roleCell(p.role, loc.terms),
                          materialCell(p.materialName, p.materialId, p.materialMissing, loc.terms),
                          formatLength(p.length, nf.length), formatLength(p.width, nf.length),
                          formatLength(p.thickness, nf.length), formatLength(p.cutLength, nf.length),
                          formatLength(p.cutWidth, nf.length), grainCell(p.grain, loc.terms),
                          edgeCell(p.edges[0], loc.terms), edgeCell(p.edges[1], loc.terms),
                          edgeCell(p.edges[2], loc.terms), edgeCell(p.edges[3], loc.terms),
                          std::to_string(p.operationCount)});
    return t;
}

Table cutListTable(const CutList& r, const Localisation& loc, const NumberFormat& nf) {
    const auto& h = loc.cutList;
    Table t;
    t.header = {h.material, h.thickness, h.cutLength, h.cutWidth, h.quantity, h.length, h.width, h.grain,
                h.rotationAllowed, h.edgeL1, h.edgeL2, h.edgeW1, h.edgeW2, h.name, h.operations, h.partNumbers,
                h.partIds};
    for (const auto& g : r.rows) {
        std::string numbers, ids;
        for (std::size_t i = 0; i < g.partIds.size(); ++i) {
            if (i) numbers += ", ", ids += ", ";
            numbers += std::to_string(g.partNumbers[i]);
            ids += g.partIds[i].str();
        }
        t.rows.push_back({materialCell(g.materialName, g.materialId, g.materialMissing, loc.terms),
                          formatLength(g.thickness, nf.length), formatLength(g.cutLength, nf.length),
                          formatLength(g.cutWidth, nf.length), std::to_string(g.quantity),
                          formatLength(g.length, nf.length), formatLength(g.width, nf.length),
                          grainCell(g.grain, loc.terms), g.rotationAllowed ? loc.terms.yes : loc.terms.no,
                          edgeCell(g.edges[0], loc.terms), edgeCell(g.edges[1], loc.terms),
                          edgeCell(g.edges[2], loc.terms), edgeCell(g.edges[3], loc.terms), g.name(),
                          std::to_string(g.operationCount), numbers, ids});
    }
    return t;
}

Table materialTable(const MaterialReport& r, const Localisation& loc, const NumberFormat& nf) {
    const auto& h = loc.materials;
    Table t;
    t.header = {h.material, h.thickness, h.parts, h.finishedArea, h.cutArea, h.sheetLength, h.sheetWidth,
                h.sheetsLowerBound, h.costPerSheet, h.costAtLowerBound, h.note};
    for (const auto& m : r.rows) {
        const bool est = m.sheetEstimateAvailable;
        t.rows.push_back(
            {materialCell(m.name, m.materialId, m.missing, loc.terms),
             m.missing ? "" : formatLength(m.thickness, nf.length), std::to_string(m.partCount),
             formatLength(m.finishedAreaM2, nf.area), formatLength(m.cutAreaM2, nf.area),
             est ? formatLength(m.sheetLength, nf.length) : "", est ? formatLength(m.sheetWidth, nf.length) : "",
             est ? std::to_string(m.sheetsLowerBoundByArea) : "",
             money(m.costPerSheet, m.costPerSheet > 0, nf), money(m.costAtAreaLowerBound, m.costKnown, nf),
             joinNotes({m.missing ? loc.terms.missing : "",
                        !m.missing && !est ? loc.terms.noSheetSize : "",
                        est ? loc.terms.areaLowerBound : "",
                        !m.missing && est && !m.costKnown ? loc.terms.priceUnknown : ""})});
    }
    return t;
}

Table edgeBandingTable(const EdgeBandingReport& r, const Localisation& loc, const NumberFormat& nf) {
    const auto& h = loc.edgeBanding;
    Table t;
    t.header = {h.edgeBand, h.thickness, h.width, h.edges, h.appliedLength, h.overhangAllowance,
                h.wasteAllowance, h.purchaseLength, h.costPerMeter, h.cost, h.note};
    for (const auto& e : r.rows)
        t.rows.push_back({e.missing ? flagged(e.edgeBandId.str(), loc.terms.missing) : e.name,
                          e.missing ? "" : formatLength(e.thickness, nf.length),
                          e.missing ? "" : formatLength(e.width, nf.length), std::to_string(e.edgeCount),
                          formatLength(e.appliedLengthM, nf.meters), formatLength(e.overhangAllowanceM, nf.meters),
                          formatLength(e.wasteAllowanceM, nf.meters), formatLength(e.purchaseLengthM, nf.meters),
                          money(e.costPerMeter, e.costKnown, nf), money(e.cost, e.costKnown, nf),
                          joinNotes({e.missing ? loc.terms.missing : "",
                                     !e.missing && !e.costKnown ? loc.terms.priceUnknown : ""})});
    return t;
}

Table hardwareTable(const HardwareReport& r, const Localisation& loc, const NumberFormat& nf) {
    const auto& h = loc.hardware;
    Table t;
    t.header = {h.hardware, h.manufacturer, h.article, h.quantity, h.unitCost, h.total, h.dimensionsVerified,
                h.note};
    for (const auto& w : r.rows)
        t.rows.push_back(
            {w.unknownDefinition ? flagged(w.definitionId.str(), loc.terms.unknownDefinition) : w.name,
             w.manufacturer, w.article, std::to_string(w.quantity), money(w.unitCost, w.costKnown, nf),
             money(w.totalCost, w.costKnown, nf), w.dimensionsVerified ? loc.terms.yes : loc.terms.no,
             joinNotes({w.unknownDefinition ? loc.terms.unknownDefinition : "",
                        !w.unknownDefinition && !w.dimensionsVerified ? loc.terms.unverified : "",
                        !w.unknownDefinition && !w.costKnown ? loc.terms.priceUnknown : ""})});
    return t;
}

Table machiningTable(const MachiningSummary& r, const Localisation& loc, const NumberFormat& nf) {
    const auto& h = loc.machining;
    Table t;
    t.header = {h.number, h.assembly, h.partId, h.name, h.holes, h.holeCount, h.grooves, h.grooveLength,
                h.pockets};
    for (const auto& m : r.rows) {
        std::string holes;
        for (const auto& g : m.holes) {
            if (!holes.empty()) holes += ", ";
            holes += std::to_string(g.count) + " x " + formatLength(g.diameter, nf.length) + " @ " +
                     domain::toString(g.face);
        }
        t.rows.push_back({std::to_string(m.partNumber), m.assemblyName, m.partId.str(), m.partName, holes,
                          std::to_string(m.holeCount), std::to_string(m.grooveCount),
                          formatLength(m.grooveLength, nf.length), std::to_string(m.pocketCount)});
    }
    return t;
}

Table labelTable(const LabelSet& r, const Localisation& loc, const NumberFormat& nf) {
    const auto& h = loc.labels;
    Table t;
    t.header = {h.number, h.project, h.assembly, h.name, h.partId, h.length, h.width, h.thickness, h.cutLength,
                h.cutWidth, h.material, h.edgeCode, h.quantity};
    for (const auto& l : r.labels)
        t.rows.push_back({std::to_string(l.partNumber), l.projectName, l.assemblyName, l.partName, l.partId.str(),
                          formatLength(l.length, nf.length), formatLength(l.width, nf.length),
                          formatLength(l.thickness, nf.length), formatLength(l.cutLength, nf.length),
                          formatLength(l.cutWidth, nf.length),
                          l.materialMissing ? flagged(l.materialName, loc.terms.missing) : l.materialName,
                          l.edgeCode, l.quantityText()});
    return t;
}

Table costTable(const CostEstimate& r, const Localisation& loc, const NumberFormat& nf) {
    const auto& h = loc.cost;
    const auto& tm = loc.terms;
    Table t;
    t.header = {h.category, h.item, h.quantity, h.unit, h.unitCost, h.cost, h.currency, h.note};
    auto subtotal = [&](const std::string& category, double value, const std::string& note) {
        t.rows.push_back({category, tm.subtotal, "", "", "", formatLength(value, nf.money), r.currency, note});
    };
    for (const auto& m : r.materials) {
        std::string note = m.missing ? tm.missing
                           : m.source == SheetCountSource::AreaLowerBound ? tm.areaLowerBound
                           : m.source == SheetCountSource::Supplied ? tm.suppliedSheetCount
                                                                    : tm.noSheetSize;
        if (!m.missing && !m.costKnown && m.source != SheetCountSource::Unavailable)
            note = joinNotes({note, tm.priceUnknown});
        t.rows.push_back({tm.materials, materialCell(m.name, m.materialId, m.missing, tm),
                          m.source == SheetCountSource::Unavailable ? "" : std::to_string(m.sheets), tm.sheets,
                          money(m.costPerSheet, m.costKnown, nf), money(m.cost, m.costKnown, nf), r.currency,
                          note});
    }
    subtotal(tm.materials, r.materialsTotal, r.materialsAtAreaLowerBound ? tm.areaLowerBound : "");
    for (const auto& e : r.edgeBanding)
        t.rows.push_back({tm.edgeBanding, e.missing ? flagged(e.edgeBandId.str(), tm.missing) : e.name,
                          formatLength(e.purchaseLengthM, nf.meters), tm.meters,
                          money(e.costPerMeter, e.costKnown, nf), money(e.cost, e.costKnown, nf), r.currency,
                          e.missing ? tm.missing : (e.costKnown ? "" : tm.priceUnknown)});
    subtotal(tm.edgeBanding, r.edgeBandingTotal, "");
    for (const auto& w : r.hardware)
        t.rows.push_back(
            {tm.hardware, w.unknownDefinition ? flagged(w.definitionId.str(), tm.unknownDefinition) : w.name,
             std::to_string(w.quantity), tm.pieces, money(w.unitCost, w.costKnown, nf),
             money(w.cost, w.costKnown, nf), r.currency,
             w.unknownDefinition ? tm.unknownDefinition : (w.costKnown ? "" : tm.priceUnknown)});
    subtotal(tm.hardware, r.hardwareTotal, "");
    t.rows.push_back({tm.total, "", "", "", "", formatLength(r.total, nf.money), r.currency,
                      joinNotes({r.materialsAtAreaLowerBound ? tm.totalIsLowerBound : "",
                                 r.complete ? "" : tm.totalIncomplete})});
    return t;
}

Table summaryTable(const ProjectSummary& r, const Localisation& loc, const NumberFormat& nf) {
    const auto& h = loc.summary;
    Table t;
    t.header = {h.assembly, h.parts, h.hardware, h.operations, h.finishedArea, h.width, h.depth, h.height};
    for (const auto& a : r.assemblies)
        t.rows.push_back({a.name, std::to_string(a.partCount), std::to_string(a.hardwareQuantity),
                          std::to_string(a.operationCount), formatLength(a.finishedAreaM2, nf.area),
                          formatLength(a.envelopeWidth, nf.length), formatLength(a.envelopeDepth, nf.length),
                          formatLength(a.envelopeHeight, nf.length)});
    return t;
}

} // namespace fos::production
