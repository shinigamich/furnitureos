// SPDX-License-Identifier: AGPL-3.0-only
// Batch outputs: thin glue from the merged batch model to the production / cutting outputs.
#include "fos/project/batch.hpp"

#include "fos/core/units.hpp"

namespace fos::project {

ProductionOptions batchProductionOptions(const Batch& batch) {
    ProductionOptions o;
    o.edgeBanding = batch.edgeBanding;
    o.cutList.groupAcrossAssemblies = batch.cutListGroupAcrossAssemblies;
    return o;
}

production::CutList batchCutList(const BatchModel& model, const Batch& batch) {
    return production::cutList(model.model, batchProductionOptions(batch).cutList);
}

CuttingPlans batchCuttingPlans(const BatchModel& model, const Batch& batch, const cutting::Options& options) {
    return cuttingPlans(model.model, batch.cutting, options);
}

production::LabelSet batchLabels(const BatchModel& model, const Batch& batch) {
    production::LabelOptions options;
    options.projectName = batch.name;
    options.cutList = batchProductionOptions(batch).cutList;
    production::LabelSet set = production::labels(model.model, options);
    for (auto& label : set.labels) {
        const std::string& id = label.partId.str();
        const BatchProjectModel* p = model.projectOf(id);
        if (p == nullptr) continue;
        const std::string instance = id.substr(0, id.find('/'));
        label.projectName = (batch.name.empty() ? std::string() : batch.name + " | ") + p->name + " | order " + instance;
    }
    return set;
}

namespace {

BatchSummaryRow summarise(const domain::Model& m, const production::EdgeBandingOptions& eb,
                          core::Diagnostics* diagnostics) {
    BatchSummaryRow row;
    for (const auto& a : m.assemblies) row.partCount += static_cast<int>(a.parts.size());
    const auto materials = production::materialReport(m);
    for (const auto& r : materials.rows) row.sheetsLowerBound += r.sheetsLowerBoundByArea;
    const auto banding = production::edgeBandingReport(m, eb);
    for (const auto& r : banding.rows) {
        row.edgeBandingAppliedM += r.appliedLengthM;
        row.edgeBandingPurchaseM += r.purchaseLengthM;
    }
    const auto hw = production::hardwareReport(m);
    for (const auto& r : hw.rows) row.hardwareQuantity += r.quantity;
    if (diagnostics != nullptr) {
        diagnostics->insert(diagnostics->end(), materials.diagnostics.begin(), materials.diagnostics.end());
        diagnostics->insert(diagnostics->end(), banding.diagnostics.begin(), banding.diagnostics.end());
        diagnostics->insert(diagnostics->end(), hw.diagnostics.begin(), hw.diagnostics.end());
    }
    return row;
}

} // namespace

BatchSummary batchSummary(const BatchModel& model, const Batch& batch) {
    BatchSummary s;
    for (const auto& p : model.projects) {
        BatchSummaryRow row = summarise(p.model, batch.edgeBanding, nullptr);
        row.key = p.key;
        row.name = p.name;
        row.quantity = p.quantity;
        row.included = p.included;
        s.projects.push_back(std::move(row));
    }
    s.total = summarise(model.model, batch.edgeBanding, &s.diagnostics);
    s.total.key = "TOTAL";
    s.total.name = batch.name;
    s.total.included = true;
    for (const auto& p : model.projects) s.total.quantity += p.quantity;
    s.hardware = production::hardwareReport(model.model);
    return s;
}

production::Table batchSummaryTable(const BatchSummary& summary) {
    production::Table t;
    t.header = {"Key", "Project", "Qty", "Included", "Parts", "Sheets (lower bound)", "Edge banding applied (m)",
                "Edge banding purchase (m)", "Hardware"};
    auto add = [&t](const BatchSummaryRow& r) {
        t.rows.push_back({r.key, r.name, std::to_string(r.quantity), r.included ? "yes" : "no",
                          std::to_string(r.partCount), std::to_string(r.sheetsLowerBound),
                          core::formatLength(r.edgeBandingAppliedM, 3), core::formatLength(r.edgeBandingPurchaseM, 3),
                          std::to_string(r.hardwareQuantity)});
    };
    for (const auto& r : summary.projects) add(r);
    add(summary.total);
    return t;
}

} // namespace fos::project
