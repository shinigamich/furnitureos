// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/app/job_pack_export.hpp"

#include "fos/app/session.hpp"
#include "fos/core/units.hpp"
#include "fos/cutting/svg.hpp"
#include "fos/nesting/svg.hpp"

#include <QCoreApplication>

#include <algorithm>

namespace fos::app {

namespace {
class JobPackText {
    Q_DECLARE_TR_FUNCTIONS(JobPackText)
};

std::string pct(double v) { return core::formatLength(v, 1) + " %"; }
} // namespace

QString jobPackSectionTitle(production::JobPackSection section) {
    using S = production::JobPackSection;
    switch (section) {
    case S::Cover: return JobPackText::tr("Cover page");
    case S::Parts: return JobPackText::tr("Part list");
    case S::CutList: return JobPackText::tr("Cut list per material");
    case S::BoughtIn: return JobPackText::tr("Bought-in items");
    case S::EdgeBanding: return JobPackText::tr("Edge banding");
    case S::Hardware: return JobPackText::tr("Hardware");
    case S::Packing: return JobPackText::tr("Packing list");
    case S::Assembly: return JobPackText::tr("Assembly sheets");
    case S::Plans: return JobPackText::tr("Cutting plans");
    case S::Warnings: return JobPackText::tr("Warnings");
    }
    return {};
}

void addCuttingPlanImages(const project::CuttingPlans& plans, production::JobPackOptions& o) {
    o.planNotes.push_back("Layouts are produced by HEURISTIC optimisers (guillotine for rectangular parts, true-shape "
                          "nesting for shaped parts); they are in general not optimal.");
    for (const auto& material : plans.materials) {
        if (material.guillotine && *material.guillotine < plans.plans.size()) {
            const auto& plan = plans.plans[*material.guillotine];
            if (!plan.result) {
                o.planNotes.push_back("Material " + plan.job.name + " (" + plan.job.id +
                                      "): optimisation failed, see warnings.");
            } else {
                const auto& sheets = plan.result->sheets;
                for (std::size_t i = 0; i < sheets.size(); ++i)
                    o.cuttingPlans.push_back(
                        {plan.job.name + " (" + plan.job.id + ") - sheet " + std::to_string(i + 1) + " of " +
                             std::to_string(sheets.size()) + ": " + sheets[i].stockId + " " +
                             core::formatLength(sheets[i].sheetLength) + " x " +
                             core::formatLength(sheets[i].sheetWidth) + " mm, " +
                             std::to_string(sheets[i].placements.size()) + " pieces, utilisation " +
                             pct(sheets[i].utilisationOfSheetPct),
                         cutting::renderSheetSvg(plan.job, sheets[i])});
            }
        }
        if (material.nesting && *material.nesting < plans.nestingPlans.size()) {
            const auto& plan = plans.nestingPlans[*material.nesting];
            const std::string name = plan.job.materialName + " (" + plan.job.materialId + ")";
            if (!plan.result) {
                o.planNotes.push_back("Material " + name + ": nesting of shaped parts failed, see warnings.");
            } else {
                const auto& sheets = plan.result->sheets;
                for (std::size_t i = 0; i < sheets.size(); ++i)
                    o.cuttingPlans.push_back({name + " - shaped parts, nest sheet " + std::to_string(i + 1) + " of " +
                                                  std::to_string(sheets.size()) + ": " + sheets[i].sheetId + ", " +
                                                  std::to_string(sheets[i].placements.size()) + " part(s)",
                                              nesting::renderNestSvg(plan.job.job, sheets[i])});
            }
        }
    }
    o.extraDiagnostics.insert(o.extraDiagnostics.end(), plans.diagnostics.begin(), plans.diagnostics.end());
}

core::Result<std::string> jobPackHtml(const project::Project& project, const project::Resolved& resolved,
                                      const project::CuttingPlans* plans, const JobPackRequest& request) {
    production::JobPackOptions o;
    o.sections = request.sections;
    o.projectName = project.metadata.name;
    o.author = project.metadata.author;
    o.date = request.date;
    const auto settings = project::productionOptions(project);
    o.cutList = settings.cutList;
    o.edgeBanding = settings.edgeBanding;
    const bool withPlans =
        std::find(o.sections.begin(), o.sections.end(), production::JobPackSection::Plans) != o.sections.end();
    if (withPlans && plans)
        addCuttingPlanImages(*plans, o);
    return production::jobPackHtml(resolved.model, o);
}

core::Result<std::string> jobPackHtml(Session& session, const JobPackRequest& request) {
    const project::Resolved* resolved = session.resolved();
    if (!resolved)
        return core::makeError(core::ErrorCode::InvalidArgument,
                               "The project cannot be resolved, so no job pack can be made: " + session.resolveError());
    const bool withPlans = std::find(request.sections.begin(), request.sections.end(),
                                     production::JobPackSection::Plans) != request.sections.end();
    const project::CuttingPlans* plans = withPlans ? &session.cuttingPlans() : nullptr;
    return jobPackHtml(session.project(), *resolved, plans, request);
}

} // namespace fos::app
