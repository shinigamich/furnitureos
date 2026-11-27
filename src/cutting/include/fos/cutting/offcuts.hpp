// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/cutting/types.hpp"

#include <vector>

/// Usable offcuts of a finished guillotine layout, for adding them to remnant stock.
///
/// The free rectangles of a guillotine layout are the Offcut leaves of its cut tree
/// (SheetLayout::offcuts): material left over after every cut, kerf already removed, so each
/// one can be sawn out of the sheet with the cuts of the plan. Rectangles are in the sheet
/// frame (+X = sheet length = grain direction of grained boards), mm.
namespace fos::cutting {

/// One usable offcut of one sheet.
struct UsableOffcut {
    int sheetIndex = -1;  ///< Index into CuttingResult::sheets (-1 from the single-layout overload).
    int offcutIndex = -1; ///< Index into SheetLayout::offcuts.
    Rect rect;            ///< In the sheet frame; rect.length runs along the sheet length (grain).
};

/// The offcuts of `layout` whose LONGER side is >= minLength and whose SHORTER side is
/// >= minWidth (sizes compared with core::kLengthEps tolerance, mm), independent of the job's
/// own remnant limits. Order: area descending, then y, then x ascending (deterministic).
/// minLength / minWidth below 0 are treated as 0; non-finite limits return nothing.
std::vector<UsableOffcut> usableOffcuts(const SheetLayout& layout, double minLength, double minWidth);

/// usableOffcuts() of every sheet of `result`, sheet by sheet (sheetIndex set).
std::vector<UsableOffcut> usableOffcuts(const CuttingResult& result, double minLength, double minWidth);

} // namespace fos::cutting
