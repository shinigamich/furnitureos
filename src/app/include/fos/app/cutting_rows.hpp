// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Cutting workspace view-model. Every number is taken from project::cuttingPlans()
/// (cutting module, a HEURISTIC optimiser); nothing is computed here.
#include "fos/project/commands.hpp"
#include "fos/project/outputs.hpp"

#include <optional>
#include <string>
#include <vector>

namespace fos::app {

struct CuttingSummaryRow {
    std::string materialId;
    std::string materialName;
    double thickness = 0;
    int pieces = 0;               ///< Piece instances in the job.
    bool optimised = false;       ///< False when optimize() failed (see diagnostics).
    int sheets = 0;               ///< Sheets in the heuristic layout.
    int sheetLowerBound = 0;      ///< Area-based lower bound of the sheet count.
    bool sheetCountProvenMinimal = false;
    double utilisationPct = 0;    ///< Totals::utilisationOfSheetPct.
    int unplaced = 0;             ///< Unplaced piece instances.
    bool layoutValid = false;     ///< Independent validateLayout() found no error.
};

std::vector<CuttingSummaryRow> cuttingSummary(const project::CuttingPlans& plans);

/// The settings of `materialId` (nullopt = project defaults) with kerf and trim replaced,
/// as commands::setCuttingSettings. All other settings (remnants, objective, ...) are kept.
project::Command cuttingSettingsCommand(const project::Project& project, const std::optional<std::string>& materialId,
                                        double kerf, const cutting::Trim& trim);

/// What the cutting settings editor submits for one scope (project defaults or a material).
struct CuttingSettingsInput {
    double kerf = 4.0;
    cutting::Trim trim{10.0, 10.0, 10.0, 10.0};
    cutting::Objective objective = cutting::Objective::MinimizeSheets;
    std::vector<project::Remnant> remnants; ///< Extra stock of the material, opened before full sheets.
};
/// Editor state of the scope (`materialId` nullopt = project defaults; a material without an
/// own entry shows the defaults).
CuttingSettingsInput cuttingSettingsInput(const project::Project& project, const std::optional<std::string>& materialId);
/// commands::setCuttingSettings with kerf, trim, objective and remnants replaced; minimum
/// remnant sizes, stages and first cut of the scope are kept. Validation (non-negative sizes,
/// positive remnant sizes, quantity >= 1) is done by the command, which refuses bad values.
project::Command cuttingSettingsCommand(const project::Project& project, const std::optional<std::string>& materialId,
                                        const CuttingSettingsInput& input);
/// True when the material has its own cutting settings entry (otherwise it uses the defaults).
bool hasOwnCuttingSettings(const project::Project& project, const std::string& materialId);
/// commands::setCuttingSettings(materialId, nullopt): the material uses the defaults again.
project::Command useDefaultCuttingSettingsCommand(const std::string& materialId);
/// Smallest unused remnant id "R<n>" (n = 1, 2, ...).
std::string nextRemnantId(const std::vector<project::Remnant>& remnants);

} // namespace fos::app
