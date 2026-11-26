// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/project/outputs.hpp"

#include "fos/cutting/validate.hpp"
#include "fos/nesting/nest.hpp"
#include "fos/nesting/validate.hpp"

#include <map>
#include <set>

namespace fos::project {

ProductionOptions productionOptions(const Project& project) {
    ProductionOptions o;
    o.edgeBanding = project.settings.edgeBanding;
    o.cutList.groupAcrossAssemblies = project.settings.cutListGroupAcrossAssemblies;
    return o;
}

ProductionReports productionReports(const Resolved& resolved, const ProductionOptions& o) {
    return productionReports(resolved.model, o);
}

ProductionReports productionReports(const domain::Model& m, const ProductionOptions& o) {
    ProductionReports r;
    r.parts = production::partList(m, o.scope);
    r.cutList = production::cutList(m, o.cutList, o.scope);
    r.boughtIn = production::boughtInList(m, o.scope);
    r.materials = production::materialReport(m, o.scope);
    r.edgeBanding = production::edgeBandingReport(m, o.edgeBanding, o.scope);
    r.hardware = production::hardwareReport(m, o.scope);
    r.machining = production::machiningSummary(m, o.machining, o.scope);
    r.summary = production::projectSummary(m, o.scope);
    return r;
}

namespace {

cutting::Settings adapterSettings(const CuttingSettings& s) {
    cutting::Settings out;
    out.kerf = s.kerf;
    out.trim = s.trim;
    out.minRemnantLength = s.minRemnantLength;
    out.minRemnantWidth = s.minRemnantWidth;
    out.maxStages = s.maxStages;
    out.firstCut = s.firstCut;
    out.objective = s.objective;
    return out;
}

void addNestingPlans(const domain::Model& model, const NestingSettings& ns, CuttingPlans& out) {
    std::vector<nesting::MaterialNestJob> jobs = nesting::nestingJobsFromModel(model, nestingModelSettings(ns), out.diagnostics);
    nesting::NestOptions options;
    options.iterations = ns.iterations;
    options.seed = ns.seed;
    for (auto& job : jobs) {
        NestingPlan plan;
        plan.job = std::move(job);
        const std::string& name = plan.job.materialName.empty() ? plan.job.materialId : plan.job.materialName;
        auto result = nesting::nest(plan.job.job, options);
        if (!result) {
            out.diagnostics.push_back({core::Severity::Error, "project.nesting.failed",
                                       "No nesting layout for the shaped parts of material \"" + name +
                                           "\": " + result.error().message,
                                       plan.job.materialId});
        } else {
            plan.validation = nesting::validateNest(plan.job.job, result.value(), options);
            for (const auto& d : plan.validation)
                if (d.severity == core::Severity::Error)
                    out.diagnostics.push_back({core::Severity::Error, "project.nesting.layout_invalid",
                                               "Nesting layout for \"" + name + "\" failed validation: " + d.message,
                                               plan.job.materialId});
            for (const auto& u : result.value().unplaced)
                out.diagnostics.push_back({core::Severity::Warning, "project.nesting.unplaced", u.message, u.partId});
            plan.result = std::move(result.value());
        }
        out.nestingPlans.push_back(std::move(plan));
    }
    // Material index in model order (both kinds).
    std::map<std::string, MaterialPlans> byId;
    for (std::size_t i = 0; i < out.plans.size(); ++i) {
        byId[out.plans[i].job.id].materialId = out.plans[i].job.id;
        byId[out.plans[i].job.id].guillotine = i;
    }
    for (std::size_t i = 0; i < out.nestingPlans.size(); ++i) {
        byId[out.nestingPlans[i].job.materialId].materialId = out.nestingPlans[i].job.materialId;
        byId[out.nestingPlans[i].job.materialId].nesting = i;
    }
    for (const auto& m : model.materials) {
        const auto it = byId.find(m.id.str());
        if (it == byId.end()) continue;
        out.materials.push_back(it->second);
        byId.erase(it);
    }
    for (auto& [id, mp] : byId) out.materials.push_back(mp); // not in the model (adapter reported it)
}

} // namespace

nesting::ModelSettings nestingModelSettings(const NestingSettings& s) {
    nesting::ModelSettings m;
    m.toolDiameter = s.toolDiameter;
    m.clearance = s.clearance;
    m.chordTolerance = s.chordTolerance;
    m.margins = s.margins;
    m.minRemnantLength = s.minRemnantLength;
    return m;
}

const CuttingPlan* CuttingPlans::guillotineFor(const std::string& materialId) const {
    for (const auto& p : plans)
        if (p.job.id == materialId) return &p;
    return nullptr;
}

const NestingPlan* CuttingPlans::nestingFor(const std::string& materialId) const {
    for (const auto& p : nestingPlans)
        if (p.job.materialId == materialId) return &p;
    return nullptr;
}

CuttingPlans cuttingPlans(const Resolved& resolved, const ProjectCuttingSettings& settings,
                          const cutting::Options& options) {
    return cuttingPlans(resolved.model, settings, options);
}

CuttingPlans cuttingPlans(const domain::Model& model, const ProjectCuttingSettings& settings,
                          const cutting::Options& options) {
    CuttingPlans out;
    cutting::ModelCutting split = cutting::cuttingFromModel(model, adapterSettings(settings.defaults), out.diagnostics);
    out.routedParts = std::move(split.routedParts);
    std::vector<cutting::Job>& jobs = split.jobs;
    for (auto& job : jobs) {
        const CuttingSettings& s = settings.forMaterial(job.id);
        job.kerf = s.kerf;
        job.trim = s.trim;
        job.minRemnantLength = s.minRemnantLength;
        job.minRemnantWidth = s.minRemnantWidth;
        job.maxStages = s.maxStages;
        job.firstCut = s.firstCut;
        job.objective = s.objective;
        for (const auto& rem : s.remnants) {
            cutting::StockSheet sheet;
            sheet.id = job.id + "/remnant/" + rem.id;
            sheet.label = rem.label.empty() ? "Remnant " + rem.id : rem.label;
            sheet.length = rem.length;
            sheet.width = rem.width;
            sheet.quantity = rem.quantity;
            sheet.cost = 0.0;
            sheet.isRemnant = true;
            sheet.priority = 1;
            job.stock.push_back(std::move(sheet));
        }
        CuttingPlan plan;
        plan.job = std::move(job);
        auto result = cutting::optimize(plan.job, options);
        if (!result) {
            out.diagnostics.push_back({core::Severity::Error, "project.cutting.optimize_failed",
                                       "No cutting plan for material \"" + plan.job.name + "\": " + result.error().message,
                                       plan.job.id});
        } else {
            plan.validation = cutting::validateLayout(plan.job, result.value());
            for (const auto& d : plan.validation)
                if (d.severity == core::Severity::Error)
                    out.diagnostics.push_back({core::Severity::Error, "project.cutting.layout_invalid",
                                               "Cutting plan for \"" + plan.job.name + "\" failed validation: " + d.message,
                                               plan.job.id});
            for (const auto& u : result.value().unplaced)
                out.diagnostics.push_back({core::Severity::Warning, "project.cutting.unplaced", u.message, u.pieceId});
            plan.result = std::move(result.value());
        }
        out.plans.push_back(std::move(plan));
    }
    addNestingPlans(model, settings.nestingSettings(), out);
    return out;
}

core::Result<drawings::DrawingResult> assemblyDrawing(const Project& project, const Resolved& resolved,
                                                      const std::string& itemId,
                                                      const drawings::AssemblyDrawingOptions& options) {
    if (resolved.model.findAssembly(domain::AssemblyId(itemId)) == nullptr)
        return core::makeError(core::ErrorCode::NotFound,
                               "Item \"" + itemId + "\" has no generated assembly to draw (missing or failed to generate).",
                               itemId);
    static const drawings::DrawingEdits kNone;
    const auto it = project.drawingEdits.find(assemblyDrawingId(itemId));
    return drawings::generateAssemblyDrawing(resolved.model, domain::AssemblyId(itemId), options,
                                             it == project.drawingEdits.end() ? kNone : it->second);
}

core::Result<drawings::DrawingResult> partDrawing(const Project& project, const Resolved& resolved,
                                                  const std::string& partId, const drawings::PartDrawingOptions& options) {
    static const drawings::DrawingEdits kNone;
    const auto it = project.drawingEdits.find(partDrawingId(partId));
    return drawings::generatePartDrawing(resolved.model, domain::PartId(partId), options,
                                         it == project.drawingEdits.end() ? kNone : it->second);
}

std::map<domain::MaterialId, production::SheetUsage> quoteSheetUsage(const CuttingPlans& plans,
                                                                     core::Diagnostics& diagnostics) {
    std::map<domain::MaterialId, production::SheetUsage> usage;
    std::set<std::string> failed;
    for (const auto& plan : plans.plans) {
        if (!plan.result) {
            failed.insert(plan.job.id);
            continue;
        }
        auto& u = usage[domain::MaterialId(plan.job.id)];
        for (const auto& sheet : plan.result->sheets) {
            const bool remnant = sheet.stockIndex >= 0 &&
                                 static_cast<std::size_t>(sheet.stockIndex) < plan.job.stock.size() &&
                                 plan.job.stock[static_cast<std::size_t>(sheet.stockIndex)].isRemnant;
            if (!remnant) ++u.sheets; // listed remnants are already owned: not purchased
            u.cutLengthMm += sheet.cutLength;
        }
    }
    for (const auto& plan : plans.nestingPlans) {
        if (!plan.result) {
            failed.insert(plan.job.materialId);
            continue;
        }
        usage[domain::MaterialId(plan.job.materialId)].sheets += static_cast<int>(plan.result->sheets.size());
    }
    for (const auto& id : failed) {
        usage.erase(domain::MaterialId(id));
        diagnostics.push_back({core::Severity::Warning, "project.quote.plan_failed",
                               "No cutting plan could be made for material '" + id +
                                   "'; it is priced by area instead of by sheets.",
                               id});
    }
    return usage;
}

} // namespace fos::project
