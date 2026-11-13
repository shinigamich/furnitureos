// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Cutting workspace view-model. Every number is taken from project::cuttingPlans()
/// (cutting module, a HEURISTIC optimiser); nothing is computed here.
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

} // namespace fos::app
