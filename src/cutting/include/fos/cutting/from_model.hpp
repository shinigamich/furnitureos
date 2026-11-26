// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/core/diagnostic.hpp"
#include "fos/cutting/types.hpp"
#include "fos/domain/model.hpp"

#include <string>
#include <vector>

namespace fos::cutting {

/// Shop settings applied to every job derived from a model.
struct Settings {
    double kerf = 4.0;  ///< Saw blade kerf, mm.
    Trim trim{10.0, 10.0, 10.0, 10.0}; ///< Edge trim per side, mm.
    double minRemnantLength = 300.0;
    double minRemnantWidth = 100.0;
    int maxStages = 0;  ///< 0 = unlimited.
    FirstCut firstCut = FirstCut::Free;
    Objective objective = Objective::MinimizeSheets;
};

/// Builds one cutting job per material that has at least one part, in the order of
/// Model::materials (assemblies and parts are visited in model order).
///
/// * Piece sizes are domain::cutSize() (saw size before edge banding), never finished sizes.
/// * canRotate = domain::canRotateOnSheet(part, &material), i.e. (part.grain == None) or
///   (material.hasGrain == false). When domain::sheetAlignment() is LengthAcrossSheet (part
///   grain AlongWidth on a grained material) the two sizes are swapped so that Piece::length
///   is the side that must run along the sheet length (the material grain).
/// * Parts with equal piece length, width (within core::kLengthEps) and canRotate are merged
///   into one Piece: id = id of the first part, quantity = count, sourceIds = all part ids,
///   label = the distinct part names joined with " / ".
/// * Stock: one unlimited entry "<material id>/sheet" of sheetLength x sheetWidth, cost
///   costPerSheet. Job::id = material id, Job::name = material name.
///
/// * Parts whose outline is not a plain rectangle are NOT included (see cuttingFromModel()).
/// * Bought-in parts (domain::isBoughtIn: glass panes) are NOT included: they are ordered cut to
///   size, and are not listed in `routedParts`. One Info diagnostic cutting.adapter.bought_in per
///   call gives the count of the RECTANGULAR ones (none when 0); shaped bought-in parts are counted
///   by nesting::nestingJobsFromModel (nesting.adapter.bought_in), so the two never overlap.
///
/// Problems are appended to `diagnostics` (never dropped silently):
///   cutting.adapter.material_missing (Error)   part references a material not in the model;
///   cutting.adapter.invalid_size (Error)       cut size is not positive;
///   cutting.adapter.invalid_sheet (Error)      material sheet size is not positive (its parts get no job);
///   cutting.adapter.thickness_mismatch (Warning) part thickness differs from the material's;
///   cutting.adapter.routed (Info)              shaped part left to routing / nesting;
///   cutting.adapter.bought_in (Info, once)     count of bought-in parts left out;
///   cutting.adapter.edgeband_missing (Warning)  an edge references an edge band not in the model
///                                               (one per edge; the band counts as 0 mm, so the
///                                               piece is cut oversize on that edge).
///
/// Target: this adapter is the separate CMake target fos::cutting_model (deps fos::cutting,
/// fos::domain); the optimiser core fos::cutting depends on fos::core only.
std::vector<Job> jobsFromModel(const domain::Model& model, const Settings& settings,
                               core::Diagnostics& diagnostics);

/// A part that is NOT cut on the panel saw because its outline is not a plain rectangle
/// (`!domain::isRectangularOutline(part)`: a contour with arcs or non-rectangular corners, or any
/// cut-out; ADR-0008). Such parts are produced by routing and go to true-shape nesting
/// (fos/nesting/from_model.hpp, target fos::nesting_model).
struct RoutedPart {
    std::string partId;
    std::string label;      ///< Part name (id when the name is empty).
    std::string materialId; ///< As referenced by the part (may be missing from the model).
};

/// Result of the routing split: guillotine jobs for rectangular parts plus the excluded shaped parts.
struct ModelCutting {
    std::vector<Job> jobs;                ///< Exactly what jobsFromModel() returns.
    std::vector<RoutedPart> routedParts;  ///< Model order (assemblies, then parts).
    int boughtInCount = 0;                ///< Rectangular bought-in parts left out (domain::isBoughtIn).
};

/// Routing split (phase 12). Rectangular parts (`domain::isRectangularOutline`) are handled
/// EXACTLY as before; every other part is left out of the guillotine jobs, listed in
/// `routedParts` and reported with an Info diagnostic `cutting.adapter.routed` (the shaped-part
/// check comes first, so a shaped part never produces the material/size diagnostics here).
/// jobsFromModel() applies the same split and returns `.jobs`; it emits the same Info diagnostics.
ModelCutting cuttingFromModel(const domain::Model& model, const Settings& settings, core::Diagnostics& diagnostics);

} // namespace fos::cutting
