// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Thin glue from a Resolved project to the derived outputs. No business logic lives here:
/// reports come from `production`, cutting plans from `cutting`, drawings from `drawings`.
#include "fos/cutting/from_model.hpp"
#include "fos/cutting/optimizer.hpp"
#include "fos/drawings/drawing.hpp"
#include "fos/nesting/from_model.hpp"
#include "fos/nesting/types.hpp"
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

/// One material's true-shape nesting job (its SHAPED parts) and the nested, independently
/// validated layout. The nesting optimiser is a HEURISTIC (result->isHeuristic is always true).
struct NestingPlan {
    nesting::MaterialNestJob job;                ///< nesting::nestingJobsFromModel (materialId, job, frames).
    std::optional<nesting::NestResult> result;   ///< Empty when nest() failed (see diagnostics).
    core::Diagnostics validation;                ///< nesting::validateNest(job.job, *result).
};

/// Where the plans of one material are (indices into CuttingPlans::plans / nestingPlans).
struct MaterialPlans {
    std::string materialId;
    std::optional<std::size_t> guillotine; ///< Rectangular parts (panel saw).
    std::optional<std::size_t> nesting;    ///< Shaped parts (routing).
};

struct CuttingPlans {
    std::vector<CuttingPlan> plans; ///< Rectangular parts; model material order (cutting::cuttingFromModel).
    /// Shaped parts (`!domain::isRectangularOutline`); model material order. A material can have
    /// a guillotine plan, a nesting plan or both.
    std::vector<NestingPlan> nestingPlans;
    /// Every material with at least one plan, in model material order.
    std::vector<MaterialPlans> materials;
    /// Parts left out of the guillotine jobs (cutting::ModelCutting::routedParts); they are in nestingPlans.
    std::vector<cutting::RoutedPart> routedParts;
    /// Guillotine adapter findings (incl. Info cutting.adapter.routed per shaped part), nesting
    /// adapter findings (nesting.adapter.*), then per guillotine job: project.cutting.optimize_failed
    /// (Error), project.cutting.layout_invalid (Error, one per validateLayout error),
    /// project.cutting.unplaced (Warning, one per unplaced piece); then per nesting job:
    /// project.nesting.failed (Error), project.nesting.layout_invalid (Error, one per validateNest
    /// error), project.nesting.unplaced (Warning, one per unplaced part instance).
    core::Diagnostics diagnostics;

    const CuttingPlan* guillotineFor(const std::string& materialId) const;
    const NestingPlan* nestingFor(const std::string& materialId) const;
};

/// nesting::ModelSettings of the project nesting settings.
nesting::ModelSettings nestingModelSettings(const NestingSettings& settings);

/// cutting::jobsFromModel with the project DEFAULT settings, then for every job (job id =
/// material id) the material's own CuttingSettings (kerf, trim, remnant limits, stages, first
/// cut, objective) and its remnants appended as limited stock
/// ("<material id>/remnant/<remnant id>", isRemnant = true, priority 1, cost 0); then
/// cutting::optimize and cutting::validateLayout. Shaped parts: nesting::nestingJobsFromModel
/// with settings.nestingSettings() (nestingModelSettings), nesting::nest (iterations, seed) and
/// nesting::validateNest.
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
