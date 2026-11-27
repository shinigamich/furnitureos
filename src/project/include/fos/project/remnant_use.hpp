// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Workshop remnant store <-> project cutting: offcut extraction from cutting plans and cutting
/// a project with store remnants offered first (reusing the project remnant support: listed
/// remnants become limited stock with isRemnant = true, priority 1, cost 0; outputs.hpp).
#include "fos/cutting/optimizer.hpp"
#include "fos/project/outputs.hpp"
#include "fos/project/remnant_store.hpp"

#include <string>
#include <vector>

namespace fos::project {

/// Prefix of the project-remnant id under which a store remnant is offered: the stock id is
/// "<material id>/remnant/store:<remnant id>".
inline constexpr const char* kStoreRemnantPrefix = "store:";

struct OffcutImportOptions {
    double minLength = 300.0; ///< Longer side of a kept offcut, mm (cutting::usableOffcuts).
    double minWidth = 100.0;  ///< Shorter side, mm.
    std::string date;         ///< dateAdded of new entries, "YYYY-MM-DD" (required).
    std::string location;     ///< location of new entries (may be empty).
    std::string source;       ///< source of new entries, e.g. "offcut of 'Kitchen' (kitchen.fosp)".
};

/// New store entries (not yet in `store`) for the usable offcuts of every guillotine plan with a
/// result, in plan order, then sheet order, then usableOffcuts order. Ids continue
/// nextRemnantId(store). length/width = offcut extent along the sheet length/width; grain =
/// AlongLength when the model material has grain, else None; thickness = job thickness; note
/// names the sheet and the offcut position. Shaped-part nesting sheets are not considered.
std::vector<StoredRemnant> remnantsFromOffcuts(const CuttingPlans& plans, const domain::Model& model,
                                               const RemnantStore& store, const OffcutImportOptions& options);

struct StoreUseOptions {
    /// Job name: remnants reserved by exactly this name may be used; other reserved ones are not.
    std::string jobName;
    /// Use cutting::Objective::PreferRemnants for materials that get store remnants.
    bool preferRemnants = false;
    cutting::Options cutting;
};

struct SkippedRemnant {
    std::string remnantId;
    std::string reason; ///< Complete English sentence.
};

struct StoreRemnantUse {
    std::string remnantId;
    std::string materialId;
    int sheetIndex = -1;  ///< Into the plan's result.sheets.
    int pieceCount = 0;   ///< Pieces cut from it.
};

struct StoreCuttingRun {
    CuttingPlans plans;
    std::vector<std::string> offered;     ///< Store ids added as stock to a guillotine job (store order).
    std::vector<SkippedRemnant> skipped;  ///< Same material as a job but not offered (thickness, reservation).
    std::vector<StoreRemnantUse> used;    ///< Plan order, then sheet order.
};

/// cuttingPlans() with the project settings plus, per guillotine material of the model, every
/// store remnant of that material id with the same thickness (within core::kLengthEps) that is
/// free or reserved by options.jobName. A remnant with grain AlongWidth is offered with length
/// and width swapped (stock length = grain direction). Remnants of other materials are ignored.
StoreCuttingRun cuttingPlansWithStore(const Resolved& resolved, const ProjectCuttingSettings& settings,
                                      const RemnantStore& store, const StoreUseOptions& options = {});

struct StoreCommit {
    RemnantStore store;                  ///< The new store content.
    std::vector<StoredRemnant> removed;  ///< Used remnants (as they were in the store).
    std::vector<StoredRemnant> added;    ///< New offcuts (remnantsFromOffcuts of run.plans).
};

/// Store after the run: used remnants removed, offcuts of every sheet (including the sheets cut
/// from store remnants) added. ConstraintViolation (store unchanged) when the plans have Error
/// diagnostics (a failed or invalid layout must not change stock) or a used id is not in the store.
core::Result<StoreCommit> commitStoreUse(const RemnantStore& store, const StoreCuttingRun& run,
                                         const domain::Model& model, const OffcutImportOptions& options);

} // namespace fos::project
