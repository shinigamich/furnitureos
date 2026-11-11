// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <string>
#include <vector>

/// Plain input/output data of the rectangular guillotine cutting optimiser.
///
/// SHEET FRAME: origin at one corner of the stock sheet, +X along the sheet LENGTH
/// (the grain direction of grained boards), +Y along the sheet WIDTH. All lengths are
/// millimetres, all areas mm^2. "length" always means the extent along X of whatever
/// frame is being talked about, "width" the extent along Y.
namespace fos::cutting {

/// StockSheet::quantity value meaning "as many sheets as needed".
inline constexpr int kUnlimited = -1;

/// Axis-aligned rectangle in the sheet frame: [x, x+length] x [y, y+width].
struct Rect {
    double x = 0;
    double y = 0;
    double length = 0; ///< Extent along X.
    double width = 0;  ///< Extent along Y.
    double area() const noexcept { return length * width; }
};

/// A required rectangular piece (saw size, i.e. before edge banding).
struct Piece {
    std::string id;     ///< Unique within the job.
    std::string label;  ///< Human-readable name used in messages and drawings.
    double length = 0;  ///< If canRotate is false this side must run along the sheet length (grain).
    double width = 0;
    int quantity = 1;   ///< >= 1.
    bool canRotate = true; ///< May be placed turned by 90 degrees.
    /// Optional, informational: ids of the model parts merged into this piece
    /// (instance k of the piece corresponds to sourceIds[k] when the sizes match).
    std::vector<std::string> sourceIds;
};

/// A type of stock sheet (full board or a listed remnant).
struct StockSheet {
    std::string id;    ///< Unique within the job.
    std::string label;
    double length = 0; ///< Along X (grain direction of grained boards).
    double width = 0;
    int quantity = kUnlimited; ///< Number of sheets available, or kUnlimited.
    double cost = 0;           ///< Per sheet; 0 = unknown.
    bool isRemnant = false;    ///< A leftover from earlier jobs (see Objective::PreferRemnants).
    int priority = 0;          ///< Higher values are opened first.
};

/// Edge trim removed from each side of every stock sheet before cutting.
/// left: x = 0 side, right: x = sheet length side, bottom: y = 0 side, top: y = sheet width side.
struct Trim {
    double left = 0;
    double right = 0;
    double top = 0;
    double bottom = 0;
};

/// Direction of a straight saw cut.
/// Lengthwise: the cut line is parallel to the sheet length (X); `position` is its y.
/// Crosswise:  the cut line is parallel to the sheet width (Y);  `position` is its x.
enum class CutOrientation { Lengthwise, Crosswise };

/// Preferred direction of the first cut through the usable area of each sheet.
enum class FirstCut { Free, Lengthwise, Crosswise };

/// What the optimiser compares candidate layouts by (see README for the exact keys).
enum class Objective { MinimizeSheets, MinimizeWaste, PreferRemnants, MinimizeCuts };

/// One cutting job: pieces of one material and thickness plus the stock to cut them from.
struct Job {
    std::string id;
    std::string name;
    double thickness = 0; ///< Informational only.
    std::vector<Piece> pieces;
    std::vector<StockSheet> stock;
    double kerf = 0; ///< Width of material removed by the saw blade, >= 0.
    Trim trim;
    /// An offcut is a reusable remnant when its longer side >= minRemnantLength and
    /// its shorter side >= minRemnantWidth; otherwise it is waste.
    double minRemnantLength = 300.0;
    double minRemnantWidth = 100.0;
    int maxStages = 0; ///< Maximum number of cutting stages; 0 = unlimited, otherwise >= 2.
    FirstCut firstCut = FirstCut::Free;
    Objective objective = Objective::MinimizeSheets;
};

/// A placed piece. The rectangle [x, x+length] x [y, y+width] is in the sheet frame.
struct Placement {
    std::string pieceId;
    int instance = 0;  ///< 0-based index among the `quantity` copies of the piece.
    double x = 0;
    double y = 0;
    double length = 0; ///< Placed extent along X (= piece width when rotated).
    double width = 0;  ///< Placed extent along Y.
    bool rotated = false;
};

enum class CutKind {
    Trim, ///< Removes an edge-trim strip; the blade runs inside the strip (kerfWidth 0).
    Split ///< Divides a piece of material inside the usable area.
};

/// One straight edge-to-edge cut through the rectangular piece of material `source`.
///
/// Split cuts remove the strip [position, position + kerfWidth] (measured along the axis
/// perpendicular to the cut line) and leave a low part [source start, position] and, if
/// anything remains, a high part [position + kerfWidth, source end]. kerfWidth equals the
/// job kerf unless less material than one kerf remains beyond `position`.
struct Cut {
    CutKind kind = CutKind::Split;
    CutOrientation orientation = CutOrientation::Crosswise;
    double position = 0;  ///< y (Lengthwise) or x (Crosswise) of the low edge of the kerf.
    double kerfWidth = 0;
    double extent = 0;    ///< Length of the cut line = full size of `source` along the cut.
    Rect source;          ///< The piece of material being cut, as it is just before this cut.
    int stage = 0;        ///< 1-based cutting stage (0 for trim cuts).
    int node = -1;        ///< Index of the corresponding CutNode, -1 for trim cuts.
};

enum class NodeKind { Cut, Piece, Offcut };

/// Node of the guillotine cut tree of one sheet. Node 0 is the usable area.
struct CutNode {
    NodeKind kind = NodeKind::Offcut;
    Rect rect;        ///< Material covered by this node before it is cut further.
    int parent = -1;
    // Cut nodes:
    CutOrientation orientation = CutOrientation::Crosswise;
    double position = 0;
    double kerfWidth = 0;
    int stage = 0;    ///< Consecutive cuts of the same orientation share a stage.
    int low = -1;     ///< Child on the low-coordinate side (always present).
    int high = -1;    ///< Child beyond the kerf, -1 when the kerf consumed the rest.
    // Leaves:
    int item = -1;    ///< Piece: index into SheetLayout::placements; Offcut: into ::offcuts.
};

/// Leftover rectangle of a sheet.
struct Offcut {
    Rect rect;
    bool reusable = false; ///< True: reusable remnant; false: waste.
};

/// Layout of one stock sheet. Area identity (exact up to rounding):
///   sheetArea = trimArea + usedArea + kerfArea + remnantArea + wasteArea.
struct SheetLayout {
    std::string stockId;
    int stockIndex = -1;     ///< Index into Job::stock.
    double sheetLength = 0;
    double sheetWidth = 0;
    Rect usable;             ///< Sheet minus edge trim.
    std::vector<Placement> placements;
    std::vector<CutNode> tree;  ///< Guillotine cut tree, root = node 0 = usable area.
    std::vector<Cut> cuts;      ///< Executable order: trim cuts, then the tree in pre-order.
    std::vector<Offcut> offcuts;
    double sheetArea = 0;
    double usableArea = 0;
    double trimArea = 0;     ///< sheetArea - usableArea.
    double usedArea = 0;     ///< Sum of the placed piece areas.
    double kerfArea = 0;     ///< Material turned to sawdust by split cuts.
    double remnantArea = 0;  ///< Sum of reusable offcuts.
    double wasteArea = 0;    ///< Sum of non-reusable offcuts.
    double utilisationOfSheetPct = 0;  ///< 100 * usedArea / sheetArea.
    double utilisationOfUsablePct = 0; ///< 100 * usedArea / usableArea.
    double cutLength = 0;    ///< Sum of Cut::extent.
    int stageCount = 0;      ///< Highest Cut::stage on this sheet.
};

enum class UnplacedReason {
    DoesNotFitAnyStock, ///< Larger than the usable area of every stock type (in every allowed orientation).
    StockExhausted      ///< Would fit, but every sheet it fits on is used up.
};

/// Copies of a piece that are not in the layout.
struct UnplacedPiece {
    std::string pieceId;
    int quantity = 0;
    UnplacedReason reason = UnplacedReason::DoesNotFitAnyStock;
    std::string message; ///< Complete English sentence naming the piece and the numbers.
};

struct StockUsage {
    std::string stockId;
    int sheetsUsed = 0;
};

/// Sums over all sheets of the result.
struct Totals {
    int sheetCount = 0;
    int placedCount = 0;
    int unplacedCount = 0;
    double sheetArea = 0;
    double usableArea = 0;
    double trimArea = 0;
    double usedArea = 0;
    double kerfArea = 0;
    double remnantArea = 0;
    double wasteArea = 0;
    double utilisationOfSheetPct = 0;  ///< 100 * usedArea / sheetArea (0 without sheets).
    double utilisationOfUsablePct = 0; ///< 100 * usedArea / usableArea.
    int cutCount = 0;      ///< Trim cuts included.
    double cutLength = 0;
    double stockCost = 0;  ///< Sum of StockSheet::cost of the used sheets.
};

/// Result of optimize(). The layout is produced by a HEURISTIC and is in general not optimal.
struct CuttingResult {
    std::string jobId;
    std::vector<SheetLayout> sheets;
    std::vector<UnplacedPiece> unplaced;
    std::vector<StockUsage> stockUsage; ///< One entry per Job::stock entry, same order.
    Totals totals;

    std::string strategy;        ///< Description of the winning heuristic combination.
    int strategiesTried = 0;     ///< Complete packing runs of the multi-strategy phase.
    int improvementRuns = 0;     ///< Complete packing runs of the improvement phase.
    bool cancelled = false;          ///< Stopped by Options::isCancelled; best layout so far.
    bool timeBudgetExhausted = false;

    /// Area-based lower bound on the number of sheets needed for the PLACED pieces:
    /// the smallest k such that the k largest available usable areas cover their area.
    int sheetCountLowerBound = 0;
    /// True only when nothing is unplaced and sheets.size() == sheetCountLowerBound. Then
    /// the sheet COUNT is provably minimal. Says nothing about waste, cuts or remnants.
    bool isSheetCountProvenMinimal = false;
    /// Always false: this module never proves a layout optimal.
    bool isProvenOptimal = false;
};

const char* toString(Objective o) noexcept;
const char* toString(CutOrientation o) noexcept;
const char* toString(FirstCut f) noexcept;
const char* toString(UnplacedReason r) noexcept;

} // namespace fos::cutting
