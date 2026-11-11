// SPDX-License-Identifier: AGPL-3.0-only
#include "packer.hpp"

#include "fos/core/units.hpp"
#include "fos/cutting/optimizer.hpp"

#include <algorithm>
#include <limits>
#include <optional>

namespace fos::cutting::detail {
namespace {

constexpr double kEps = core::kLengthEps;
constexpr double kInf = std::numeric_limits<double>::infinity();

int stageFor(const PackedSheet& sheet, int node, CutOrientation o) {
    const int parent = sheet.nodes[static_cast<std::size_t>(node)].parent;
    if (parent < 0) return 1;
    const CutNode& p = sheet.nodes[static_cast<std::size_t>(parent)];
    return p.orientation == o ? p.stage : p.stage + 1;
}

struct Candidate {
    int node = -1;
    bool rotated = false;
    bool crossFirst = false;
    double primary = kInf;
    double secondary = kInf;
    bool betterThan(const Candidate& o) const {
        if (node < 0) return false;
        if (o.node < 0) return true;
        if (primary < o.primary - kEps) return true;
        if (primary > o.primary + kEps) return false;
        return secondary < o.secondary - kEps;
    }
};

class Packer {
public:
    Packer(const Job& job, const std::vector<Instance>& instances, const Strategy& strategy, int singleStock,
           int singleStockLimit)
        : job_(job), inst_(instances), s_(strategy) {
        const std::size_t n = job.stock.size();
        usable_.resize(n);
        remaining_.resize(n);
        for (std::size_t i = 0; i < n; ++i) {
            usable_[i] = usableArea(job.stock[i], job.trim);
            remaining_[i] = job.stock[i].quantity;
            if (singleStock >= 0) remaining_[i] = static_cast<int>(i) == singleStock ? singleStockLimit : 0;
            stockOrder_.push_back(static_cast<int>(i));
        }
        const bool remnantsFirst = job.objective == Objective::PreferRemnants;
        const bool smallFirst = s_.stock == StockRule::SmallFirst;
        std::stable_sort(stockOrder_.begin(), stockOrder_.end(), [&](int a, int b) {
            const StockSheet& sa = job.stock[static_cast<std::size_t>(a)];
            const StockSheet& sb = job.stock[static_cast<std::size_t>(b)];
            if (remnantsFirst && sa.isRemnant != sb.isRemnant) return sa.isRemnant;
            if (sa.priority != sb.priority) return sa.priority > sb.priority;
            const double aa = usable_[static_cast<std::size_t>(a)].area();
            const double ab = usable_[static_cast<std::size_t>(b)].area();
            if (aa != ab) return smallFirst ? aa < ab : aa > ab;
            return false;
        });
    }

    PackResult run(const std::vector<int>& order) {
        // Area of the pieces still to be processed (for StockRule::FitRemaining).
        std::vector<double> suffix(order.size() + 1, 0.0);
        for (std::size_t i = order.size(); i-- > 0;) {
            const Instance& in = inst_[static_cast<std::size_t>(order[i])];
            suffix[i] = suffix[i + 1] + in.length * in.width;
        }
        for (std::size_t i = 0; i < order.size(); ++i) {
            if (!placeInstance(order[i], suffix[i])) res_.unplaced.push_back(order[i]);
        }
        return std::move(res_);
    }

private:
    bool placeInstance(int idx, double remainingArea) {
        const Instance& in = inst_[static_cast<std::size_t>(idx)];
        const double area = in.length * in.width;
        // 1. First sheet (in opening order) with a fitting free rectangle.
        for (PackedSheet& sheet : res_.sheets) {
            if (sheet.freeArea + kEps < area) continue;
            const Candidate c = findInSheet(sheet, in);
            if (c.node >= 0) {
                place(sheet, idx, c);
                return true;
            }
        }
        // 2. Re-split a leftover pair on an open sheet so that the piece fits.
        for (PackedSheet& sheet : res_.sheets) {
            if (sheet.freeArea + kEps < area) continue;
            if (placeAfterFlip(sheet, idx)) return true;
        }
        // 3. Open a new sheet.
        const int stock = chooseStock(in, remainingArea);
        if (stock < 0) return false;
        PackedSheet sheet;
        sheet.stock = stock;
        sheet.usable = usable_[static_cast<std::size_t>(stock)];
        CutNode root;
        root.kind = NodeKind::Offcut;
        root.rect = sheet.usable;
        sheet.nodes.push_back(root);
        sheet.freeNodes.push_back(0);
        sheet.freeArea = sheet.usable.area();
        const Candidate c = findInSheet(sheet, in);
        if (c.node < 0) return false; // Cannot happen for maxStages 0 or >= 2; stay safe.
        if (remaining_[static_cast<std::size_t>(stock)] > 0) --remaining_[static_cast<std::size_t>(stock)];
        res_.sheets.push_back(std::move(sheet));
        place(res_.sheets.back(), idx, c);
        return true;
    }

    int chooseStock(const Instance& in, double remainingArea) const {
        int best = -1;
        int largest = -1;
        for (int st : stockOrder_) {
            const std::size_t k = static_cast<std::size_t>(st);
            if (remaining_[k] == 0 || !fitsRect(in, usable_[k])) continue;
            if (s_.stock != StockRule::FitRemaining) return st;
            const double a = usable_[k].area();
            if (largest < 0 || a > usable_[static_cast<std::size_t>(largest)].area()) largest = st;
            if (a + kEps >= remainingArea && (best < 0 || a < usable_[static_cast<std::size_t>(best)].area()))
                best = st;
        }
        return best >= 0 ? best : largest;
    }

    /// Order of the two cuts around a piece of size pl x pw in the free node; nullopt if
    /// the piece does not fit or the stage limit forbids it.
    std::optional<bool> splitChoice(const PackedSheet& sheet, int node, double pl, double pw) const {
        const CutNode& f = sheet.nodes[static_cast<std::size_t>(node)];
        const double dl = f.rect.length - pl;
        const double dw = f.rect.width - pw;
        if (dl < -kEps || dw < -kEps) return std::nullopt;
        const bool needCross = dl > kEps;
        const bool needLength = dw > kEps;
        const int limit = job_.maxStages;
        const int stCross = stageFor(sheet, node, CutOrientation::Crosswise);
        const int stLength = stageFor(sheet, node, CutOrientation::Lengthwise);
        if (!needCross && !needLength) return true;
        if (needCross && !needLength) return (limit == 0 || stCross <= limit) ? std::optional<bool>(true) : std::nullopt;
        if (!needCross) return (limit == 0 || stLength <= limit) ? std::optional<bool>(false) : std::nullopt;
        const bool okCross = limit == 0 || stCross + 1 <= limit;
        const bool okLength = limit == 0 || stLength + 1 <= limit;
        if (!okCross && !okLength) return std::nullopt;
        if (okCross != okLength) return okCross;
        if (f.parent < 0 && job_.firstCut != FirstCut::Free) return job_.firstCut == FirstCut::Crosswise;
        const double k = job_.kerf;
        const double areaCross = std::max(0.0, dl - k) * f.rect.width; // larger rectangle if cross first
        const double areaLength = f.rect.length * std::max(0.0, dw - k);
        switch (s_.split) {
        case SplitRule::ShorterLeftover: return !(dl <= dw);
        case SplitRule::LongerLeftover: return dl <= dw;
        case SplitRule::MaxArea: return areaCross >= areaLength;
        case SplitRule::MinArea: return areaCross < areaLength;
        case SplitRule::LengthwiseFirst: return false;
        case SplitRule::CrosswiseFirst: return true;
        }
        return true;
    }

    Candidate bestFor(const PackedSheet& sheet, double pl, double pw, bool rotated) const {
        Candidate best;
        for (int node : sheet.freeNodes) {
            const std::optional<bool> order = splitChoice(sheet, node, pl, pw);
            if (!order) continue;
            const Rect& r = sheet.nodes[static_cast<std::size_t>(node)].rect;
            const double areaFit = r.area() - pl * pw;
            const double shortFit = std::min(r.length - pl, r.width - pw);
            Candidate c;
            c.node = node;
            c.rotated = rotated;
            c.crossFirst = *order;
            c.primary = s_.fit == FitRule::BestArea ? areaFit : shortFit;
            c.secondary = s_.fit == FitRule::BestArea ? shortFit : areaFit;
            if (c.betterThan(best)) best = c;
        }
        return best;
    }

    Candidate findInSheet(const PackedSheet& sheet, const Instance& in) const {
        const Candidate natural = bestFor(sheet, in.length, in.width, false);
        const bool square = core::approxEqual(in.length, in.width);
        if (!in.canRotate || square) return natural;
        const Candidate rotated = bestFor(sheet, in.width, in.length, true);
        switch (s_.orient) {
        case OrientRule::Either: return rotated.betterThan(natural) ? rotated : natural;
        case OrientRule::PreferNatural: return natural.node >= 0 ? natural : rotated;
        case OrientRule::PreferRotated: return rotated.node >= 0 ? rotated : natural;
        }
        return natural;
    }

    int addNode(PackedSheet& sheet, NodeKind kind, const Rect& rect, int parent) const {
        CutNode n;
        n.kind = kind;
        n.rect = rect;
        n.parent = parent;
        sheet.nodes.push_back(n);
        return static_cast<int>(sheet.nodes.size()) - 1;
    }

    /// Turns leaf `node` into a cut at `offset` from its low edge. Returns {low, high}
    /// child indices; high is -1 when the kerf consumes the rest, otherwise it joins the
    /// free list. The low child is left for the caller (it becomes a cut or the piece).
    std::pair<int, int> cutNode(PackedSheet& sheet, int node, CutOrientation o, double offset) const {
        const std::size_t ni = static_cast<std::size_t>(node);
        const Rect r = sheet.nodes[ni].rect;
        const bool cross = o == CutOrientation::Crosswise;
        const double total = cross ? r.length : r.width;
        const double rest = total - offset;
        const int stage = stageFor(sheet, node, o);
        Rect lowRect = r, highRect = r;
        if (cross) {
            lowRect.length = offset;
            highRect.x = r.x + offset + job_.kerf;
            highRect.length = rest - job_.kerf;
        } else {
            lowRect.width = offset;
            highRect.y = r.y + offset + job_.kerf;
            highRect.width = rest - job_.kerf;
        }
        const int low = addNode(sheet, NodeKind::Offcut, lowRect, node);
        int high = -1;
        if (rest - job_.kerf > kEps) {
            high = addNode(sheet, NodeKind::Offcut, highRect, node);
            sheet.freeNodes.push_back(high);
        }
        CutNode& n = sheet.nodes[ni];
        n.kind = NodeKind::Cut;
        n.orientation = o;
        n.position = (cross ? r.x : r.y) + offset;
        n.kerfWidth = std::min(job_.kerf, rest);
        n.stage = stage;
        n.low = low;
        n.high = high;
        return {low, high};
    }

    void place(PackedSheet& sheet, int idx, const Candidate& c) const {
        const Instance& in = inst_[static_cast<std::size_t>(idx)];
        const double pl = c.rotated ? in.width : in.length;
        const double pw = c.rotated ? in.length : in.width;
        const Rect f = sheet.nodes[static_cast<std::size_t>(c.node)].rect;
        sheet.freeNodes.erase(std::find(sheet.freeNodes.begin(), sheet.freeNodes.end(), c.node));
        const bool needCross = f.length - pl > kEps;
        const bool needLength = f.width - pw > kEps;
        int leaf = c.node;
        if (needCross && needLength) {
            const CutOrientation first = c.crossFirst ? CutOrientation::Crosswise : CutOrientation::Lengthwise;
            const CutOrientation second = c.crossFirst ? CutOrientation::Lengthwise : CutOrientation::Crosswise;
            leaf = cutNode(sheet, leaf, first, c.crossFirst ? pl : pw).first;
            leaf = cutNode(sheet, leaf, second, c.crossFirst ? pw : pl).first;
        } else if (needCross) {
            leaf = cutNode(sheet, leaf, CutOrientation::Crosswise, pl).first;
        } else if (needLength) {
            leaf = cutNode(sheet, leaf, CutOrientation::Lengthwise, pw).first;
        }
        CutNode& n = sheet.nodes[static_cast<std::size_t>(leaf)];
        n.kind = NodeKind::Piece;
        n.item = static_cast<int>(sheet.placed.size());
        sheet.placed.push_back({idx, f.x, f.y, pl, pw, c.rotated});
        recomputeFreeArea(sheet);
    }

    static void recomputeFreeArea(PackedSheet& sheet) {
        double a = 0;
        for (int n : sheet.freeNodes) a += sheet.nodes[static_cast<std::size_t>(n)].rect.area();
        sheet.freeArea = a;
    }

    bool placeAfterFlip(PackedSheet& sheet, int idx) const;

    const Job& job_;
    const std::vector<Instance>& inst_;
    Strategy s_;
    std::vector<Rect> usable_;
    std::vector<int> remaining_;
    std::vector<int> stockOrder_;
    PackResult res_;
};

/// A "leftover pair": cut node N whose low child A is a cut of the other orientation with
/// a piece-sized low child, and where both high children (the two leftover rectangles) are
/// still untouched free leaves. Such a pair can be re-cut in the other order.
bool isFlippable(const Job& job, const PackedSheet& sheet, int n) {
    const CutNode& N = sheet.nodes[static_cast<std::size_t>(n)];
    if (N.kind != NodeKind::Cut || N.high < 0 || N.low < 0) return false;
    const CutNode& A = sheet.nodes[static_cast<std::size_t>(N.low)];
    if (A.kind != NodeKind::Cut || A.orientation == N.orientation || A.high < 0) return false;
    if (sheet.nodes[static_cast<std::size_t>(N.high)].kind != NodeKind::Offcut) return false;
    if (sheet.nodes[static_cast<std::size_t>(A.high)].kind != NodeKind::Offcut) return false;
    if (N.parent < 0 && job.firstCut != FirstCut::Free) {
        const CutOrientation preferred =
            job.firstCut == FirstCut::Crosswise ? CutOrientation::Crosswise : CutOrientation::Lengthwise;
        if (N.orientation == preferred) return false;
    }
    if (job.maxStages != 0) {
        const int newStage = stageFor(sheet, n, A.orientation) + 1; // stage of the inner cut after the flip
        if (newStage > job.maxStages) return false;
    }
    return true;
}

/// Swaps the order of the two cuts of a leftover pair. Applying it twice restores the tree.
void flip(const Job& job, PackedSheet& sheet, int n) {
    CutNode& N = sheet.nodes[static_cast<std::size_t>(n)];
    const int a = N.low;
    CutNode& A = sheet.nodes[static_cast<std::size_t>(a)];
    const bool wasCrossFirst = N.orientation == CutOrientation::Crosswise;
    const int rightNode = wasCrossFirst ? N.high : A.high; // leftover beyond the piece along X
    const int topNode = wasCrossFirst ? A.high : N.high;   // leftover beyond the piece along Y
    const Rect f = N.rect;
    const Rect p = sheet.nodes[static_cast<std::size_t>(A.low)].rect;
    const double k = job.kerf;
    const double pl = wasCrossFirst ? A.rect.length : p.length;
    const double pw = wasCrossFirst ? p.width : A.rect.width;
    const double restL = f.length - pl - k;
    const double restW = f.width - pw - k;
    const CutOrientation outer = wasCrossFirst ? CutOrientation::Lengthwise : CutOrientation::Crosswise;
    const CutOrientation inner = wasCrossFirst ? CutOrientation::Crosswise : CutOrientation::Lengthwise;
    const int outerStage = stageFor(sheet, n, outer);
    CutNode& R = sheet.nodes[static_cast<std::size_t>(rightNode)];
    CutNode& T = sheet.nodes[static_cast<std::size_t>(topNode)];
    N.orientation = outer;
    N.stage = outerStage;
    A.orientation = inner;
    A.stage = outerStage + 1;
    if (wasCrossFirst) { // now lengthwise first: full-length top strip, short right rectangle
        N.position = f.y + pw;
        A.rect = Rect{f.x, f.y, f.length, pw};
        A.position = f.x + pl;
        T.rect = Rect{f.x, f.y + pw + k, f.length, restW};
        R.rect = Rect{f.x + pl + k, f.y, restL, pw};
        N.high = topNode;
        A.high = rightNode;
        T.parent = n;
        R.parent = a;
    } else { // now crosswise first: full-width right strip, short top rectangle
        N.position = f.x + pl;
        A.rect = Rect{f.x, f.y, pl, f.width};
        A.position = f.y + pw;
        R.rect = Rect{f.x + pl + k, f.y, restL, f.width};
        T.rect = Rect{f.x, f.y + pw + k, pl, restW};
        N.high = rightNode;
        A.high = topNode;
        R.parent = n;
        T.parent = a;
    }
    double area = 0;
    for (int fn : sheet.freeNodes) area += sheet.nodes[static_cast<std::size_t>(fn)].rect.area();
    sheet.freeArea = area;
}

bool Packer::placeAfterFlip(PackedSheet& sheet, int idx) const {
    const Instance& in = inst_[static_cast<std::size_t>(idx)];
    const int count = static_cast<int>(sheet.nodes.size());
    for (int n = 0; n < count; ++n) {
        if (!isFlippable(job_, sheet, n)) continue;
        flip(job_, sheet, n);
        const Candidate c = findInSheet(sheet, in);
        if (c.node >= 0) {
            place(sheet, idx, c);
            return true;
        }
        flip(job_, sheet, n); // restore
    }
    return false;
}

} // namespace

bool fitsRect(const Instance& inst, const Rect& rect) noexcept {
    if (inst.length <= rect.length + kEps && inst.width <= rect.width + kEps) return true;
    return inst.canRotate && inst.width <= rect.length + kEps && inst.length <= rect.width + kEps;
}

PackResult pack(const Job& job, const std::vector<Instance>& instances, const std::vector<int>& order,
                const Strategy& strategy, int singleStock, int singleStockLimit) {
    Packer packer(job, instances, strategy, singleStock, singleStockLimit);
    PackResult result = packer.run(order);
    for (PackedSheet& sheet : result.sheets) improveOffcuts(job, sheet);
    return result;
}

void improveOffcuts(const Job& job, PackedSheet& sheet) {
    const auto value = [&](int n) {
        const CutNode& N = sheet.nodes[static_cast<std::size_t>(n)];
        const Rect& r1 = sheet.nodes[static_cast<std::size_t>(N.high)].rect;
        const Rect& r2 = sheet.nodes[static_cast<std::size_t>(sheet.nodes[static_cast<std::size_t>(N.low)].high)].rect;
        const double reusable = (isReusableRemnant(job, r1.length, r1.width) ? r1.area() : 0.0) +
                                (isReusableRemnant(job, r2.length, r2.width) ? r2.area() : 0.0);
        return std::pair<double, double>(reusable, std::max(r1.area(), r2.area()));
    };
    const int count = static_cast<int>(sheet.nodes.size());
    for (int n = 0; n < count; ++n) {
        if (!isFlippable(job, sheet, n)) continue;
        const auto before = value(n);
        flip(job, sheet, n);
        const auto after = value(n);
        const bool better = after.first > before.first + 1e-3 ||
                            (after.first > before.first - 1e-3 && after.second > before.second + 1e-3);
        if (!better) flip(job, sheet, n);
    }
}

} // namespace fos::cutting::detail
