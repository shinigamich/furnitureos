// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/core/diagnostic.hpp"
#include "fos/cutting/types.hpp"
#include "fos/domain/model.hpp"

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
/// Problems are appended to `diagnostics` (never dropped silently):
///   cutting.adapter.material_missing (Error)   part references a material not in the model;
///   cutting.adapter.invalid_size (Error)       cut size is not positive;
///   cutting.adapter.invalid_sheet (Error)      material sheet size is not positive (its parts get no job);
///   cutting.adapter.thickness_mismatch (Warning) part thickness differs from the material's;
///   cutting.adapter.edgeband_missing (Warning)  an edge references an edge band not in the model
///                                               (one per edge; the band counts as 0 mm, so the
///                                               piece is cut oversize on that edge).
///
/// Target: this adapter is the separate CMake target fos::cutting_model (deps fos::cutting,
/// fos::domain); the optimiser core fos::cutting depends on fos::core only.
std::vector<Job> jobsFromModel(const domain::Model& model, const Settings& settings,
                               core::Diagnostics& diagnostics);

} // namespace fos::cutting
