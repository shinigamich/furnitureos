// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/core/diagnostic.hpp"
#include "fos/cutting/types.hpp"

namespace fos::cutting {

/// Independent check of a cutting result against its job. It shares no bookkeeping with
/// the optimiser: everything is recomputed from the job, the placements, the flat cut
/// sequence and the offcut list. An empty return value (or only non-errors) means:
///  * every sheet uses an existing stock type with matching size, within stock quantities;
///  * every placement refers to a known piece, has the piece's size, a legal rotation,
///    a unique instance index and lies inside the usable area (tolerance core::kLengthEps);
///  * any two pieces on a sheet are at least one kerf apart along X or along Y;
///  * placed + unplaced == required quantity for every piece;
///  * replaying the cut sequence on the full sheet is a valid guillotine sequence (every
///    cut runs edge to edge through an existing piece of material, kerf widths and stages
///    are right, the stage limit holds) and leaves exactly the placements and the offcuts;
///  * offcut classification, all reported areas, utilisations, totals, stock usage and the
///    lower-bound flags are consistent.
/// Codes are "cutting.layout.*"; messages name the sheet (1-based) and the numbers.
core::Diagnostics validateLayout(const Job& job, const CuttingResult& result);

} // namespace fos::cutting
