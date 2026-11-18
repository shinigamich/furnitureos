// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Cutting workspace view-model of the shaped (routed) parts: true-shape nesting summary and
/// settings. Every number is taken from project::cuttingPlans() (nesting module, a HEURISTIC);
/// nothing is computed here.
#include "fos/project/commands.hpp"
#include "fos/project/outputs.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fos::app {

/// One material's nesting plan (CuttingPlans::nestingPlans).
struct NestingSummaryRow {
    std::string materialId;
    std::string materialName;
    double thickness = 0;      ///< mm.
    int parts = 0;             ///< Part instances in the nesting job (sum of quantities).
    bool nested = false;       ///< False when nest() failed (see diagnostics).
    int sheets = 0;            ///< Sheets in the heuristic layout.
    double utilisationPct = 0; ///< NestResult::utilisation * 100 (outline area / sheet area).
    int unplaced = 0;          ///< NestResult::unplaced.size().
    int problems = 0;          ///< Error findings of the independent validateNest() (0 = valid).
    int iterationsRun = 0;     ///< NestResult::iterationsRun.
    bool heuristic = true;     ///< NestResult::isHeuristic (always true).
};

std::vector<NestingSummaryRow> nestingSummary(const project::CuttingPlans& plans);

enum class PlanKind { Guillotine, Nesting };

/// One row of the Cutting summary table: a material's guillotine plan or its nesting plan.
struct CuttingTableRow {
    std::string materialId;
    PlanKind kind = PlanKind::Guillotine;
    std::size_t planIndex = 0; ///< Into CuttingPlans::plans (Guillotine) or nestingPlans (Nesting).
};

/// CuttingPlans::materials order; per material the guillotine plan first, then the nesting plan.
std::vector<CuttingTableRow> cuttingTableRows(const project::CuttingPlans& plans);

/// The project nesting settings (stored value or the defaults).
project::NestingSettings nestingSettingsInput(const project::Project& project);
/// commands::setNestingSettings(settings); the command checks the ranges (checkNestingSettings)
/// and refuses bad values with a message naming the field.
project::Command nestingSettingsCommand(const project::NestingSettings& settings);
/// A seed typed by the user: a whole number 0 ..= 4294967295 (surrounding blanks allowed).
std::optional<std::uint32_t> parseNestingSeed(std::string_view text);

} // namespace fos::app
