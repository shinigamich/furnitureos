// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/app/cutting_rows.hpp"

#include <algorithm>

namespace fos::app {

std::vector<CuttingSummaryRow> cuttingSummary(const project::CuttingPlans& plans) {
    std::vector<CuttingSummaryRow> rows;
    for (const auto& plan : plans.plans) {
        CuttingSummaryRow row;
        row.materialId = plan.job.id;
        row.materialName = plan.job.name;
        row.thickness = plan.job.thickness;
        for (const auto& piece : plan.job.pieces) row.pieces += piece.quantity;
        if (plan.result) {
            const auto& r = *plan.result;
            row.optimised = true;
            row.sheets = static_cast<int>(r.sheets.size());
            row.sheetLowerBound = r.sheetCountLowerBound;
            row.sheetCountProvenMinimal = r.isSheetCountProvenMinimal;
            row.utilisationPct = r.totals.utilisationOfSheetPct;
            row.unplaced = r.totals.unplacedCount;
            row.layoutValid = !core::hasErrors(plan.validation);
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

project::Command cuttingSettingsCommand(const project::Project& project, const std::optional<std::string>& materialId,
                                        double kerf, const cutting::Trim& trim) {
    project::CuttingSettings settings =
        materialId ? project.cutting.forMaterial(*materialId) : project.cutting.defaults;
    settings.kerf = kerf;
    settings.trim = trim;
    return project::commands::setCuttingSettings(materialId, settings);
}

CuttingSettingsInput cuttingSettingsInput(const project::Project& project, const std::optional<std::string>& materialId) {
    const project::CuttingSettings& s = materialId ? project.cutting.forMaterial(*materialId) : project.cutting.defaults;
    return {s.kerf, s.trim, s.objective, s.remnants};
}

project::Command cuttingSettingsCommand(const project::Project& project, const std::optional<std::string>& materialId,
                                        const CuttingSettingsInput& input) {
    project::CuttingSettings settings =
        materialId ? project.cutting.forMaterial(*materialId) : project.cutting.defaults;
    settings.kerf = input.kerf;
    settings.trim = input.trim;
    settings.objective = input.objective;
    settings.remnants = input.remnants;
    return project::commands::setCuttingSettings(materialId, settings);
}

bool hasOwnCuttingSettings(const project::Project& project, const std::string& materialId) {
    return project.cutting.materials.count(materialId) != 0;
}

project::Command useDefaultCuttingSettingsCommand(const std::string& materialId) {
    return project::commands::setCuttingSettings(materialId, std::nullopt);
}

std::string nextRemnantId(const std::vector<project::Remnant>& remnants) {
    for (int n = 1;; ++n) {
        const std::string id = "R" + std::to_string(n);
        if (std::none_of(remnants.begin(), remnants.end(), [&](const project::Remnant& r) { return r.id == id; }))
            return id;
    }
}

} // namespace fos::app
