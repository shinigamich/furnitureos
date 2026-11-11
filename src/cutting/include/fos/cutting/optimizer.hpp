// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/core/diagnostic.hpp"
#include "fos/core/result.hpp"
#include "fos/cutting/types.hpp"

#include <cstdint>
#include <functional>

namespace fos::cutting {

/// Controls of optimize(). The defaults give fully deterministic results.
struct Options {
    /// Maximum number of extra packing runs in the improvement phase (0 disables it).
    int improvementRuns = 150;
    /// Wall-clock budget in milliseconds for the whole call, 0 = none. A non-zero budget
    /// makes the result depend on machine speed (it is then no longer reproducible).
    int timeBudgetMs = 0;
    /// Seed of the internal linear congruential generator used by the improvement phase.
    std::uint32_t seed = 20240229u;
    /// Polled between packing runs; return true to stop. May be empty.
    std::function<bool()> isCancelled;
};

/// Input check. Returns Error diagnostics (codes "cutting.job.*") for: empty or duplicate
/// ids, non-finite or non-positive sizes, quantity < 1 (pieces) or < 0 and != kUnlimited
/// (stock), negative kerf/trim/remnant limits, maxStages not 0 or >= 2, stock without usable
/// area after trim, no stock at all, more than 100000 piece instances.
core::Diagnostics validateJob(const Job& job);

/// Usable area of a stock sheet: the sheet minus the edge trim (may be empty or inverted
/// for invalid input; validateJob() reports that).
Rect usableArea(const StockSheet& stock, const Trim& trim) noexcept;

/// True when an offcut of the given size counts as a reusable remnant for `job`.
bool isReusableRemnant(const Job& job, double length, double width) noexcept;

/// Area-based lower bound on the sheet count: the smallest k such that the k largest
/// usable areas among the available stock sheets sum to at least `pieceArea` (mm^2).
/// Returns 0 for pieceArea <= 0 and -1 when the whole (limited) stock is too small.
/// This bound ignores shapes and kerf, so it can be lower than what is really achievable.
int areaLowerBoundOnSheetCount(const Job& job, double pieceArea);

/// Computes a guillotine cutting layout with a deterministic multi-strategy HEURISTIC
/// (see README.md). Never claims optimality; see CuttingResult::isSheetCountProvenMinimal.
///
/// Errors: InvalidArgument when validateJob() reports errors (message = first finding);
/// Cancelled when Options::isCancelled returned true before the first complete layout.
core::Result<CuttingResult> optimize(const Job& job, const Options& options = {});

} // namespace fos::cutting
