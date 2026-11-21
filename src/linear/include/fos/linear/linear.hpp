// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// 1D cutting-stock optimisation: cuts pieces from bars / profiles / rolls. HEURISTIC
/// (first-fit / best-fit decreasing over several orders + bounded improvement); see README.md.
#include "fos/core/result.hpp"
#include "fos/linear/types.hpp"

namespace fos::linear {

/// Checks the job. InvalidArgument (message names the entity and numbers) for: no pieces or no
/// stock, empty/duplicate ids, non-finite or non-positive piece length, quantity < 1, negative
/// stock quantity or cost, negative / non-finite kerf, trims or minimum offcut, a stock length
/// not exceeding trimStart + trimEnd, or more than 200000 piece instances.
core::Result<void> validateJob(const LinearJob& job);

/// Usable length of a bar of `stockLength`: stockLength - trimStart - trimEnd.
double usableLength(const LinearJob& job, double stockLength) noexcept;

/// Optimises the job (deterministic for a given job, including `seed`). Pieces that cannot be
/// placed are listed in `unplaced` with a reason; that is not an error. Errors: validateJob().
core::Result<LinearResult> optimizeLinear(const LinearJob& job);

/// Independent check of a plan against its job: recomputes cut positions (trims, kerf gaps,
/// order, bar end), piece lengths, quantities (every instance placed or listed unplaced exactly
/// once), stock availability, kerf counts / losses, offcuts, utilisation and totals.
/// Returns Error diagnostics (codes "linear.validate.*"); empty = the plan is consistent.
core::Diagnostics validateLinearPlan(const LinearJob& job, const LinearResult& result);

} // namespace fos::linear
