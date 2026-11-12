// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/core/diagnostic.hpp"
#include "fos/nesting/types.hpp"

namespace fos::nesting {

/// Independent check of a nesting result that does NOT trust the optimiser. Every outline is
/// re-derived from the ORIGINAL Part::outline and the placement's mirror/rotation/translation.
/// Error diagnostics (codes "nesting.result.*"):
///  unknown_sheet, unknown_part, bad_instance, duplicate_instance, rotation_not_allowed,
///  mirror_not_allowed, outline_mismatch (stored outline differs from the re-derived one by
///  more than 1e-6 mm), outside_area (vertex outside the placement area), spacing (two outlines
///  closer than partSpacing - 1e-6, or overlapping when partSpacing is 0), quantity (placed +
///  unplaced != quantity), stock (more sheets of a type than available), area (reported areas,
///  utilisation or used length differ from recomputed values), not_heuristic.
/// An empty result means the layout is valid. `options` supplies the default rotation step.
core::Diagnostics validateNest(const Job& job, const NestResult& result, const NestOptions& options = {});

} // namespace fos::nesting
