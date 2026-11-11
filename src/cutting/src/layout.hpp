// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
// Internal: turns packed sheets into the public result structures and computes statistics.
#include "packer.hpp"

namespace fos::cutting::detail {

/// Aggregate numbers of one packing run, used to compare runs.
struct RunStats {
    double unplacedArea = 0;
    int unplacedCount = 0;
    int sheets = 0;
    int fullSheets = 0;       ///< Sheets that are not listed remnants.
    double sheetArea = 0;
    double fullSheetArea = 0;
    double usedArea = 0;
    double remnantArea = 0;   ///< Reusable offcuts.
    double largestRemnant = 0;
    int cuts = 0;             ///< Trim cuts included.
    double cutLength = 0;
    double cost = 0;
    /// Everything that is neither a piece nor a reusable remnant (trim + kerf + waste offcuts).
    double lostArea() const noexcept { return sheetArea - usedArea - remnantArea; }
};

RunStats computeStats(const Job& job, const std::vector<Instance>& instances, const PackResult& run);

/// Builds the public layout of one sheet (placements, tree, cut sequence, offcuts, areas).
SheetLayout buildSheetLayout(const Job& job, const std::vector<Instance>& instances, const PackedSheet& sheet);

/// Fills CuttingResult::totals and ::stockUsage from the sheets and unplaced list.
void fillTotals(const Job& job, CuttingResult& result);

} // namespace fos::cutting::detail
