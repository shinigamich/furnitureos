// SPDX-License-Identifier: AGPL-3.0-only
// Workshop remnant store <-> project cutting plans.
#include "fos/project/remnant_use.hpp"

#include "fos/core/units.hpp"
#include "fos/cutting/offcuts.hpp"

#include <algorithm>
#include <cmath>
#include <set>

namespace fos::project {

std::vector<StoredRemnant> remnantsFromOffcuts(const CuttingPlans& plans, const domain::Model& model,
                                               const RemnantStore& store, const OffcutImportOptions& options) {
    std::vector<StoredRemnant> out;
    RemnantStore ids = store; // grows with the new entries so that ids keep increasing
    for (const auto& plan : plans.plans) {
        if (!plan.result) continue;
        const domain::Material* material = model.findMaterial(domain::MaterialId(plan.job.id));
        const bool grained = material != nullptr && material->hasGrain;
        for (const auto& o : cutting::usableOffcuts(*plan.result, options.minLength, options.minWidth)) {
            const auto& sheet = plan.result->sheets[static_cast<std::size_t>(o.sheetIndex)];
            StoredRemnant r;
            r.id = nextRemnantId(ids);
            r.materialId = plan.job.id;
            r.length = o.rect.length;
            r.width = o.rect.width;
            r.thickness = plan.job.thickness;
            r.grain = grained ? RemnantGrain::AlongLength : RemnantGrain::None;
            r.location = options.location;
            r.dateAdded = options.date;
            r.source = options.source;
            r.note = "offcut of sheet " + std::to_string(o.sheetIndex + 1) + " (" + sheet.stockId + ") at x " +
                     core::formatLength(o.rect.x) + ", y " + core::formatLength(o.rect.y);
            ids.remnants.push_back(r);
            out.push_back(std::move(r));
        }
    }
    return out;
}

StoreCuttingRun cuttingPlansWithStore(const Resolved& resolved, const ProjectCuttingSettings& settings,
                                      const RemnantStore& store, const StoreUseOptions& options) {
    StoreCuttingRun run;
    ProjectCuttingSettings s = settings;
    std::set<std::string> offeredIds;
    for (const auto& material : resolved.model.materials) {
        const std::string matId = material.id.value;
        std::vector<Remnant> extra;
        for (const auto& r : store.remnants) {
            if (r.materialId != matId) continue;
            if (std::abs(r.thickness - material.thickness) > core::kLengthEps) {
                run.skipped.push_back({r.id, "Remnant '" + r.id + "' is " + core::formatLength(r.thickness) +
                                                 " mm thick but material '" + matId + "' is " +
                                                 core::formatLength(material.thickness) + " mm."});
                continue;
            }
            if (!r.reservedBy.empty() && r.reservedBy != options.jobName) {
                run.skipped.push_back({r.id, "Remnant '" + r.id + "' is reserved by '" + r.reservedBy + "'."});
                continue;
            }
            Remnant rem;
            rem.id = std::string(kStoreRemnantPrefix) + r.id;
            rem.label = "Store remnant " + r.id;
            const bool swap = r.grain == RemnantGrain::AlongWidth;
            rem.length = swap ? r.width : r.length;
            rem.width = swap ? r.length : r.width;
            rem.quantity = 1;
            extra.push_back(std::move(rem));
            offeredIds.insert(r.id);
        }
        if (extra.empty()) continue;
        CuttingSettings ms = s.forMaterial(matId);
        for (auto& rem : extra) ms.remnants.push_back(std::move(rem));
        if (options.preferRemnants) ms.objective = cutting::Objective::PreferRemnants;
        s.materials[matId] = std::move(ms);
    }
    run.plans = cuttingPlans(resolved, s, options.cutting);

    std::set<std::string> jobMaterials;
    for (const auto& plan : run.plans.plans) jobMaterials.insert(plan.job.id);
    for (const auto& r : store.remnants)
        if (offeredIds.count(r.id) != 0 && jobMaterials.count(r.materialId) != 0) run.offered.push_back(r.id);

    for (const auto& plan : run.plans.plans) {
        if (!plan.result) continue;
        const std::string prefix = plan.job.id + "/remnant/" + kStoreRemnantPrefix;
        for (std::size_t i = 0; i < plan.result->sheets.size(); ++i) {
            const auto& sheet = plan.result->sheets[i];
            if (sheet.stockId.rfind(prefix, 0) != 0) continue;
            run.used.push_back({sheet.stockId.substr(prefix.size()), plan.job.id, static_cast<int>(i),
                                static_cast<int>(sheet.placements.size())});
        }
    }
    return run;
}

core::Result<StoreCommit> commitStoreUse(const RemnantStore& store, const StoreCuttingRun& run,
                                         const domain::Model& model, const OffcutImportOptions& options) {
    if (core::hasErrors(run.plans.diagnostics))
        return core::makeError(core::ErrorCode::ConstraintViolation,
                               "The cutting plans have errors; the remnant store was not changed.");
    StoreCommit c;
    std::set<std::string> usedIds;
    for (const auto& u : run.used) {
        const StoredRemnant* r = store.find(u.remnantId);
        if (r == nullptr)
            return core::makeError(core::ErrorCode::ConstraintViolation,
                                   "Remnant '" + u.remnantId + "' was cut but is not in the store; the store was not changed.",
                                   u.remnantId);
        if (usedIds.insert(u.remnantId).second) c.removed.push_back(*r);
    }
    c.added = remnantsFromOffcuts(run.plans, model, store, options);
    c.store.extra = store.extra;
    for (const auto& r : store.remnants)
        if (usedIds.count(r.id) == 0) c.store.remnants.push_back(r);
    for (const auto& r : c.added) c.store.remnants.push_back(r);
    return c;
}

} // namespace fos::project
