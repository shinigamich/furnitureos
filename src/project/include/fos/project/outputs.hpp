// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Thin glue from a Resolved project to the derived outputs. No business logic lives here:
/// reports come from `production`, cutting plans from `cutting`, drawings from `drawings`.
#include "fos/cutting/from_model.hpp"
#include "fos/cutting/optimizer.hpp"
#include "fos/drawings/drawing.hpp"
#include "fos/production/production.hpp"
#include "fos/project/resolve.hpp"

namespace fos::project {

struct ProductionOptions {
    production::Scope scope;
    production::CutListOptions cutList;
    production::EdgeBandingOptions edgeBanding;
    production::MachiningOptions machining;
};

/// Options taken from project.settings (edge banding allowances, cut-list grouping).
ProductionOptions productionOptions(const Project& project);

struct ProductionReports {
    production::PartList parts;
    production::CutList cutList;
    production::MaterialReport materials;
    production::EdgeBandingReport edgeBanding;
    production::HardwareReport hardware;
    production::MachiningSummary machining;
    production::ProjectSummary summary;
};

/// All production reports of resolved.model (each generator's own diagnostics stay in its report).
ProductionReports productionReports(const Resolved& resolved, const ProductionOptions& options = {});

/// One material's cutting job and its optimised, independently validated layout.
struct CuttingPlan {
    cutting::Job job;
    std::optional<cutting::CuttingResult> result; ///< Empty when optimize() failed (see diagnostics).
    core::Diagnostics validation;                 ///< cutting::validateLayout(job, *result).
};

struct CuttingPlans {
    std::vector<CuttingPlan> plans; ///< Model material order (cutting::jobsFromModel).
    /// Adapter findings, then per job: project.cutting.optimize_failed (Error),
    /// project.cutting.layout_invalid (Error, one per validateLayout error),
    /// project.cutting.unplaced (Warning, one per unplaced piece).
    core::Diagnostics diagnostics;
};

/// cutting::jobsFromModel with the project DEFAULT settings, then for every job (job id =
/// material id) the material's own CuttingSettings (kerf, trim, remnant limits, stages, first
/// cut, objective) and its remnants appended as limited stock
/// ("<material id>/remnant/<remnant id>", isRemnant = true, priority 1, cost 0); then
/// cutting::optimize and cutting::validateLayout.
CuttingPlans cuttingPlans(const Resolved& resolved, const ProjectCuttingSettings& settings,
                          const cutting::Options& options = {});

/// Assembly drawing of item `itemId` with the stored edits of assemblyDrawingId(itemId)
/// applied. NotFound when the item has no generated assembly.
core::Result<drawings::DrawingResult> assemblyDrawing(const Project& project, const Resolved& resolved,
                                                      const std::string& itemId,
                                                      const drawings::AssemblyDrawingOptions& options = {});

/// Part drawing of `partId` with the stored edits of partDrawingId(partId) applied.
core::Result<drawings::DrawingResult> partDrawing(const Project& project, const Resolved& resolved,
                                                  const std::string& partId,
                                                  const drawings::PartDrawingOptions& options = {});

} // namespace fos::project
