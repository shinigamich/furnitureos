// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Data types of the 1D cutting-stock optimiser (`fos::linear`). All lengths are millimetres,
/// measured along the bar from its start (position 0) to its end (position = stock length).
///
/// Bar layout semantics (identical in spirit to the sheet cutter `fos::cutting`):
///  * `trimStart` / `trimEnd` are removed at each bar end; the trim includes its own saw cut
///    (no kerf is added next to a trim). The usable range is [trimStart, length - trimEnd].
///  * The first piece starts exactly at `trimStart`; each further piece starts `kerf` after the
///    end of the previous one.
///  * No kerf is needed at the bar end: a piece that exactly fills the remaining usable length
///    fits. If material remains after the last piece, one separating cut removes
///    min(kerf, remaining); what is left after it is the offcut.
#include "fos/core/diagnostic.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace fos::linear {

/// A required piece (cut length). `quantity` identical instances are cut.
struct Piece {
    std::string id;    ///< Unique, non-empty.
    std::string label; ///< Display text (may be empty: the id is shown).
    double length = 0; ///< mm, > 0.
    int quantity = 1;  ///< >= 1.
};

/// A stock bar / roll type.
struct Stock {
    std::string id;         ///< Unique, non-empty.
    double length = 0;      ///< mm; must exceed trimStart + trimEnd.
    int quantity = 0;       ///< Available bars (>= 0); ignored when `unlimited`.
    bool unlimited = false; ///< Any number of bars may be used.
    double cost = 0;        ///< Cost of one bar (any currency), >= 0; 0 = unknown.
    bool isOffcut = false;  ///< A reusable offcut from earlier jobs (PreferOffcuts uses these first).
    int priority = 0;       ///< Lower value = opened first (before the objective's own ordering).
};

enum class Objective {
    MinimizeBars,  ///< Fewest bars, then least waste.
    MinimizeWaste, ///< Least waste (stock length - piece length - reusable offcut), then fewest bars.
    PreferOffcuts, ///< Least NEW (non-offcut) stock length consumed, then fewest bars, then waste.
};

struct LinearJob {
    std::vector<Piece> pieces;
    std::vector<Stock> stock;
    double kerf = 0;              ///< Saw blade width, mm, >= 0.
    double trimStart = 0;         ///< Removed at the bar start, mm, >= 0.
    double trimEnd = 0;           ///< Removed at the bar end, mm, >= 0.
    /// Offcuts at least this long are reported as reusable; 0 = no offcut is reusable.
    double minReusableOffcut = 0;
    Objective objective = Objective::MinimizeBars;
    std::uint64_t seed = 1;       ///< Seed of the randomised orders (deterministic).
    int iterations = 64;          ///< Randomised orders tried after the deterministic ones, >= 0.
};

/// One piece instance placed on a bar.
struct Cut {
    std::string pieceId;
    int instance = 0;  ///< 0-based instance of the piece (0 .. quantity-1).
    double start = 0;  ///< Position of the piece start on the bar, mm.
    double length = 0; ///< Piece length, mm (end = start + length).
};

struct BarPlan {
    std::string stockId;
    double stockLength = 0;
    bool isOffcut = false;       ///< Copied from the stock type.
    std::vector<Cut> cuts;       ///< Ordered by `start`.
    double trimStart = 0, trimEnd = 0;
    int kerfCount = 0;           ///< Saw cuts between pieces plus the separating cut (if any).
    double kerfLoss = 0;         ///< Material removed by those cuts, mm.
    double offcutStart = 0;      ///< mm (end of the bar region minus `offcutLength`).
    double offcutLength = 0;     ///< mm; 0 when nothing is left.
    bool offcutReusable = false; ///< offcutLength >= minReusableOffcut > 0.
    double pieceLength = 0;      ///< Sum of the cut piece lengths, mm.
    double utilisation = 0;      ///< pieceLength / stockLength (0..1).
};

enum class UnplacedReason {
    LongerThanEveryStock, ///< Longer than the usable length of every stock type.
    StockExhausted,       ///< Would fit a stock type, but no bar with room is left.
};

struct Unplaced {
    std::string pieceId;
    int instance = 0;
    double length = 0;
    UnplacedReason reason = UnplacedReason::StockExhausted;
    std::string message; ///< Specific English sentence naming the piece and the numbers.
};

/// Result of optimizeLinear(). A HEURISTIC result: the bar count is optimal only when
/// `isBarCountProvenMinimal` is true.
struct LinearResult {
    std::vector<BarPlan> bars;
    std::vector<Unplaced> unplaced;
    int requiredCount = 0; ///< Total piece instances.
    int placedCount = 0;
    /// ceil(sum(length + kerf) / (max usable length + kerf)) over all placeable instances:
    /// a valid lower bound on the bar count (n pieces on one bar use sum(length) + (n-1) kerf).
    int barCountLowerBound = 0;
    /// True only when nothing is unplaced and bars.size() == barCountLowerBound.
    bool isBarCountProvenMinimal = false;
    double totalStockLength = 0, totalPieceLength = 0, totalKerfLoss = 0, totalTrim = 0;
    double totalReusableOffcut = 0;
    double totalWaste = 0;   ///< Trims + kerf + non-reusable offcuts, mm.
    double utilisation = 0;  ///< totalPieceLength / totalStockLength (0 when no bars).
    double totalCost = 0;    ///< Sum of the stock costs of the used bars.
    int ordersTried = 0;     ///< Piece orders evaluated by the heuristic.
    core::Diagnostics diagnostics;
};

const char* toString(Objective o) noexcept;
const char* toString(UnplacedReason r) noexcept;

} // namespace fos::linear
