// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
// Internal: one deterministic guillotine packing run for a fixed piece order and strategy.
#include "fos/cutting/types.hpp"

#include <vector>

namespace fos::cutting::detail {

/// One copy of a piece. length/width as given in the job (unrotated).
struct Instance {
    int piece = 0;    ///< Index into Job::pieces.
    int instance = 0; ///< 0-based copy number.
    double length = 0;
    double width = 0;
    bool canRotate = true;
};

enum class SortKey { Area, LongerSide, ShorterSide, Perimeter, Length };
/// How the L-shaped leftover around a freshly placed piece is cut into two rectangles.
enum class SplitRule {
    ShorterLeftover, ///< Full-size strip along the axis with the smaller leftover.
    LongerLeftover,
    MaxArea,         ///< Make the larger of the two free rectangles as large as possible.
    MinArea,         ///< The opposite (more balanced rectangles).
    LengthwiseFirst, ///< Always rip first (strips along the sheet length).
    CrosswiseFirst
};
enum class FitRule { BestArea, BestShortSide };
enum class OrientRule { Either, PreferNatural, PreferRotated };
enum class StockRule { LargeFirst, SmallFirst, FitRemaining };

struct Strategy {
    SortKey sort = SortKey::Area;
    SplitRule split = SplitRule::ShorterLeftover;
    FitRule fit = FitRule::BestArea;
    OrientRule orient = OrientRule::Either;
    StockRule stock = StockRule::LargeFirst;
};

struct Placed {
    int inst = 0; ///< Index into the instance list.
    double x = 0, y = 0, length = 0, width = 0;
    bool rotated = false;
};

struct PackedSheet {
    int stock = 0; ///< Index into Job::stock.
    Rect usable;
    std::vector<CutNode> nodes;  ///< Cut tree; Offcut leaves are the free rectangles.
    std::vector<int> freeNodes;  ///< Indices of the Offcut leaves.
    std::vector<Placed> placed;
    double freeArea = 0;
};

struct PackResult {
    std::vector<PackedSheet> sheets;
    std::vector<int> unplaced; ///< Instance indices, in processing order.
};

/// Packs the instances in the given order (indices into `instances`). `singleStock` >= 0
/// restricts the run to at most `singleStockLimit` sheets of that stock type.
PackResult pack(const Job& job, const std::vector<Instance>& instances, const std::vector<int>& order,
                const Strategy& strategy, int singleStock = -1, int singleStockLimit = 0);

/// Guillotine-valid re-splitting of leftover pairs ("remnant merging"): where the two free
/// rectangles created around a piece are both still untouched, choose the split that gives
/// more reusable remnant area (then the larger single offcut).
void improveOffcuts(const Job& job, PackedSheet& sheet);

/// True if the instance fits the rectangle in some allowed orientation.
bool fitsRect(const Instance& inst, const Rect& rect) noexcept;

} // namespace fos::cutting::detail
