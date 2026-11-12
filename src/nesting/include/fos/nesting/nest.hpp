// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/core/diagnostic.hpp"
#include "fos/core/result.hpp"
#include "fos/nesting/types.hpp"

#include <vector>

namespace fos::nesting {

/// Input check. Error diagnostics (codes "nesting.job.*") for: no sheets / no parts, empty or
/// duplicate ids, non-finite or non-positive sizes, quantity < 1 (parts) or < 0 and !=
/// kUnlimited (sheets), negative margins/spacings, empty placement area, outline not simple
/// or of zero area, hole not simple or not inside the outline, rotation step outside
/// (0, 360], more than 10000 part instances or more than 1000 outline vertices.
core::Diagnostics validateJob(const Job& job, const NestOptions& options = {});

/// Placement area of a sheet: the sheet inset by (margin + job.edgeSpacing) on every side
/// (sheet frame; empty when nothing remains).
geometry::Rect2 placementArea(const Job& job, const Sheet& sheet) noexcept;

/// The allowed rotation angles of `part` in degrees, ascending in [0, 360).
std::vector<double> allowedRotations(const Part& part, const NestOptions& options);

/// True-shape nesting with a deterministic bottom-left-fill HEURISTIC plus a bounded,
/// seeded improvement loop (see README.md). Never claims optimality (isHeuristic = true).
/// Errors: InvalidArgument when validateJob() reports errors (message = first finding);
/// Cancelled when isCancelled returned true before the first complete layout.
core::Result<NestResult> nest(const Job& job, const NestOptions& options = {});

} // namespace fos::nesting
