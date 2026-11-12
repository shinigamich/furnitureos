// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/project/outputs.hpp"

#include "fos/cutting/validate.hpp"

namespace fos::project {

ProductionOptions productionOptions(const Project& project) {
    ProductionOptions o;
    o.edgeBanding = project.settings.edgeBanding;
    o.cutList.groupAcrossAssemblies = project.settings.cutListGroupAcrossAssemblies;
    return o;
}

ProductionReports productionReports(const Resolved& resolved, const ProductionOptions& o) {
    const domain::Model& m = resolved.model;
    ProductionReports r;
    r.parts = production::partList(m, o.scope);
    r.cutList = production::cutList(m, o.cutList, o.scope);
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

} // namespace

CuttingPlans cuttingPlans(const Resolved& resolved, const ProjectCuttingSettings& settings,
                          const cutting::Options& options) {
    CuttingPlans out;
    std::vector<cutting::Job> jobs = cutting::jobsFromModel(resolved.model, adapterSettings(settings.defaults), out.diagnostics);
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

} // namespace fos::project
