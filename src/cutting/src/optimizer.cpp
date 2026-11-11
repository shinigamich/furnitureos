// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/cutting/optimizer.hpp"

#include "fos/core/units.hpp"
#include "layout.hpp"
#include "packer.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>

namespace fos::cutting {
namespace {

using namespace detail;
using core::formatLength;

const char* name(SortKey k) {
    switch (k) {
    case SortKey::Area: return "area";
    case SortKey::LongerSide: return "longer-side";
    case SortKey::ShorterSide: return "shorter-side";
    case SortKey::Perimeter: return "perimeter";
    case SortKey::Length: return "length";
    }
    return "?";
}
const char* name(SplitRule r) {
    switch (r) {
    case SplitRule::ShorterLeftover: return "shorter-leftover-axis";
    case SplitRule::LongerLeftover: return "longer-leftover-axis";
    case SplitRule::MaxArea: return "max-area";
    case SplitRule::MinArea: return "min-area";
    case SplitRule::LengthwiseFirst: return "lengthwise-first";
    case SplitRule::CrosswiseFirst: return "crosswise-first";
    }
    return "?";
}
const char* name(FitRule r) { return r == FitRule::BestArea ? "best-area-fit" : "best-short-side-fit"; }
const char* name(OrientRule r) {
    switch (r) {
    case OrientRule::Either: return "best-of-both";
    case OrientRule::PreferNatural: return "prefer-unrotated";
    case OrientRule::PreferRotated: return "prefer-rotated";
    }
    return "?";
}
const char* name(StockRule r) {
    switch (r) {
    case StockRule::LargeFirst: return "largest-first";
    case StockRule::SmallFirst: return "smallest-first";
    case StockRule::FitRemaining: return "fit-remaining-area";
    }
    return "?";
}

std::string describe(const Strategy& s) {
    return std::string("heuristic: sort=") + name(s.sort) + ", split=" + name(s.split) + ", fit=" + name(s.fit) +
           ", orientation=" + name(s.orient) + ", stock=" + name(s.stock);
}

double sortValue(const Instance& in, SortKey key) {
    switch (key) {
    case SortKey::Area: return in.length * in.width;
    case SortKey::LongerSide: return std::max(in.length, in.width);
    case SortKey::ShorterSide: return std::min(in.length, in.width);
    case SortKey::Perimeter: return in.length + in.width;
    case SortKey::Length: return in.length;
    }
    return 0;
}

/// Indices of `subset` sorted by decreasing key; ties by area, longer side, then input order.
std::vector<int> sortedOrder(const std::vector<Instance>& inst, std::vector<int> subset, SortKey key) {
    std::stable_sort(subset.begin(), subset.end(), [&](int a, int b) {
        const Instance& ia = inst[static_cast<std::size_t>(a)];
        const Instance& ib = inst[static_cast<std::size_t>(b)];
        const double ka = sortValue(ia, key), kb = sortValue(ib, key);
        if (ka != kb) return ka > kb;
        const double aa = ia.length * ia.width, ab = ib.length * ib.width;
        if (aa != ab) return aa > ab;
        const double la = std::max(ia.length, ia.width), lb = std::max(ib.length, ib.width);
        if (la != lb) return la > lb;
        return a < b;
    });
    return subset;
}

using Key = std::array<double, 8>;

Key scoreKey(Objective o, const RunStats& s) {
    const double ua = s.unplacedArea, uc = s.unplacedCount, sh = s.sheets, lost = s.lostArea();
    const double cuts = s.cuts, big = -s.largestRemnant;
    switch (o) {
    case Objective::MinimizeSheets: return {ua, uc, sh, lost, s.cost, cuts, s.cutLength, big};
    case Objective::MinimizeWaste: return {ua, uc, lost, sh, s.cost, cuts, s.cutLength, big};
    case Objective::PreferRemnants:
        return {ua, uc, static_cast<double>(s.fullSheets), s.fullSheetArea, sh, lost, cuts, big};
    case Objective::MinimizeCuts: return {ua, uc, sh, cuts, s.cutLength, lost, s.cost, big};
    }
    return {};
}

/// Strict lexicographic "a is better than b" with a relative tolerance against rounding noise.
bool better(const Key& a, const Key& b) {
    for (std::size_t i = 0; i < a.size(); ++i) {
        const double tol = 1e-9 * std::max({1.0, std::fabs(a[i]), std::fabs(b[i])});
        if (a[i] < b[i] - tol) return true;
        if (a[i] > b[i] + tol) return false;
    }
    return false;
}

std::vector<Strategy> strategyList(const Job& job, const std::vector<Instance>& inst) {
    bool anyRotatable = false;
    for (const Instance& in : inst)
        if (in.canRotate && !core::approxEqual(in.length, in.width)) anyRotatable = true;
    int usableStock = 0;
    for (const StockSheet& s : job.stock)
        if (s.quantity != 0) ++usableStock;
    std::vector<OrientRule> orients{OrientRule::Either};
    if (anyRotatable) {
        orients.push_back(OrientRule::PreferNatural);
        orients.push_back(OrientRule::PreferRotated);
    }
    std::vector<StockRule> stocks{StockRule::LargeFirst};
    if (usableStock > 1) {
        stocks.push_back(StockRule::SmallFirst);
        stocks.push_back(StockRule::FitRemaining);
    }
    std::vector<Strategy> list;
    for (StockRule st : stocks)
        for (SortKey sort : {SortKey::Area, SortKey::LongerSide, SortKey::ShorterSide, SortKey::Perimeter,
                             SortKey::Length})
            for (SplitRule split : {SplitRule::ShorterLeftover, SplitRule::LongerLeftover, SplitRule::MaxArea,
                                    SplitRule::MinArea, SplitRule::LengthwiseFirst, SplitRule::CrosswiseFirst})
                for (FitRule fit : {FitRule::BestArea, FitRule::BestShortSide})
                    for (OrientRule orient : orients) list.push_back({sort, split, fit, orient, st});
    return list;
}

std::string sizeText(double l, double w) { return formatLength(l) + " \xC3\x97 " + formatLength(w); }

std::string pieceName(const Piece& p) { return "'" + (p.label.empty() ? p.id : p.label) + "'"; }

std::string noFitMessage(const Job& job, const Piece& p) {
    Rect largest;
    bool fitsRotated = false;
    int types = 0;
    for (const StockSheet& s : job.stock) {
        const Rect u = usableArea(s, job.trim);
        ++types;
        if (u.area() > largest.area()) largest = u;
        if (p.width <= u.length + core::kLengthEps && p.length <= u.width + core::kLengthEps) fitsRotated = true;
    }
    std::string m = "Piece " + pieceName(p) + " " + sizeText(p.length, p.width) + " does not fit the usable area ";
    if (types == 1)
        m += sizeText(largest.length, largest.width) + " of any stock sheet";
    else
        m += "of any stock sheet (largest usable area " + sizeText(largest.length, largest.width) + ")";
    if (!p.canRotate && fitsRotated)
        m += "; it would fit turned by 90\xC2\xB0, but rotation is not allowed for this piece (grain)";
    return m + ".";
}

std::string exhaustedMessage(const Job& job, const Piece& p, int count) {
    const Instance in{0, 0, p.length, p.width, p.canRotate};
    int sheets = 0;
    for (const StockSheet& s : job.stock)
        if (s.quantity > 0 && fitsRect(in, usableArea(s, job.trim))) sheets += s.quantity;
    return std::to_string(count) + " of " + std::to_string(p.quantity) + " required copies of piece " +
           pieceName(p) + " " + sizeText(p.length, p.width) + " could not be placed: the " +
           std::to_string(sheets) + " available stock sheet(s) large enough for it are used up.";
}

/// Deterministic 32-bit linear congruential generator (Numerical Recipes constants).
class Lcg {
public:
    explicit Lcg(std::uint32_t seed) : x_(seed) {}
    std::size_t below(std::size_t n) {
        x_ = x_ * 1664525u + 1013904223u;
        return static_cast<std::size_t>(x_ >> 8) % n;
    }

private:
    std::uint32_t x_;
};

struct Best {
    bool valid = false;
    PackResult run;
    RunStats stats;
    Key key{};
    Strategy strategy;
    std::vector<int> order;
    bool improved = false;
};

/// Index of the sheet with the least placed area (ties: the later sheet).
int worstSheet(const PackResult& run) {
    int worst = -1;
    double least = 0;
    for (std::size_t i = 0; i < run.sheets.size(); ++i) {
        double used = 0;
        for (const Placed& p : run.sheets[i].placed) used += p.length * p.width;
        if (worst < 0 || used <= least) {
            worst = static_cast<int>(i);
            least = used;
        }
    }
    return worst;
}

} // namespace

core::Result<CuttingResult> optimize(const Job& job, const Options& options) {
    const core::Diagnostics problems = validateJob(job);
    for (const core::Diagnostic& d : problems)
        if (d.severity == core::Severity::Error)
            return core::makeError(core::ErrorCode::InvalidArgument, d.message, d.entityId);

    const auto started = std::chrono::steady_clock::now();
    bool cancelled = false, timedOut = false;
    const auto shouldStop = [&](bool haveLayout) {
        if (options.isCancelled && options.isCancelled()) {
            cancelled = true;
            return true;
        }
        if (haveLayout && options.timeBudgetMs > 0) {
            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - started).count();
            if (ms >= options.timeBudgetMs) {
                timedOut = true;
                return true;
            }
        }
        return false;
    };

    // Expand pieces into instances; pieces that fit no stock type at all never enter packing.
    std::vector<Instance> instances;
    std::vector<int> noFit(job.pieces.size(), 0);
    for (std::size_t pi = 0; pi < job.pieces.size(); ++pi) {
        const Piece& p = job.pieces[pi];
        Instance in{static_cast<int>(pi), 0, p.length, p.width, p.canRotate};
        bool fits = false;
        for (const StockSheet& s : job.stock)
            if (fitsRect(in, usableArea(s, job.trim))) fits = true;
        if (!fits) {
            noFit[pi] = p.quantity;
            continue;
        }
        for (int k = 0; k < p.quantity; ++k) {
            in.instance = k;
            instances.push_back(in);
        }
    }
    std::vector<int> all(instances.size());
    for (std::size_t i = 0; i < all.size(); ++i) all[i] = static_cast<int>(i);

    const auto evaluate = [&](PackResult&& run, const Strategy& s, const std::vector<int>& order, Best& best,
                              bool fromImprovement) {
        const RunStats stats = computeStats(job, instances, run);
        const Key key = scoreKey(job.objective, stats);
        if (best.valid && !better(key, best.key)) return false;
        best.valid = true;
        best.run = std::move(run);
        best.stats = stats;
        best.key = key;
        best.strategy = s;
        best.order = order;
        best.improved = best.improved || fromImprovement;
        return true;
    };

    // Phase 1: every strategy combination once.
    Best best;
    int tried = 0, improvementRuns = 0;
    const std::vector<Strategy> strategies = strategyList(job, instances);
    bool stopped = false;
    for (const Strategy& s : strategies) {
        if (shouldStop(best.valid)) {
            stopped = true;
            break;
        }
        const std::vector<int> order = sortedOrder(instances, all, s.sort);
        evaluate(pack(job, instances, order, s), s, order, best, false);
        ++tried;
        if (instances.empty()) break; // nothing to vary
    }
    if (!best.valid)
        return core::makeError(core::ErrorCode::Cancelled,
                               "Cutting optimisation of job '" + job.id +
                                   "' was cancelled before the first layout was complete.",
                               job.id);

    // Phase 2 (bounded): perturb the piece order of the best run, then re-pack its emptiest sheet.
    const int budget = std::max(0, options.improvementRuns);
    const int orderRuns = budget - budget / 3;
    Lcg rng(options.seed);
    for (int i = 0; !stopped && i < orderRuns && instances.size() > 1 && !best.run.sheets.empty(); ++i) {
        if (shouldStop(true)) {
            stopped = true;
            break;
        }
        std::vector<int> order = best.order;
        const std::size_t n = order.size();
        if (i == 0) {
            // Pieces of the emptiest sheet first: tries to dissolve that sheet into the others.
            const int w = worstSheet(best.run);
            std::vector<char> inWorst(instances.size(), 0);
            for (const Placed& p : best.run.sheets[static_cast<std::size_t>(w)].placed)
                inWorst[static_cast<std::size_t>(p.inst)] = 1;
            std::stable_partition(order.begin(), order.end(),
                                  [&](int idx) { return inWorst[static_cast<std::size_t>(idx)] != 0; });
        } else {
            switch (rng.below(3)) {
            case 0: std::swap(order[rng.below(n)], order[rng.below(n)]); break;
            case 1: {
                const std::size_t from = rng.below(n);
                std::rotate(order.begin(), order.begin() + static_cast<std::ptrdiff_t>(from),
                            order.begin() + static_cast<std::ptrdiff_t>(from) + 1);
                break;
            }
            default: {
                const std::size_t a = rng.below(n);
                const std::size_t len = std::min(n - a, 2 + rng.below(6));
                std::reverse(order.begin() + static_cast<std::ptrdiff_t>(a),
                             order.begin() + static_cast<std::ptrdiff_t>(a + len));
                break;
            }
            }
        }
        const Strategy s = best.strategy;
        evaluate(pack(job, instances, order, s), s, order, best, true);
        ++improvementRuns;
    }
    if (!stopped && budget / 3 > 0 && !best.run.sheets.empty()) {
        const std::size_t w = static_cast<std::size_t>(worstSheet(best.run));
        std::vector<int> subset;
        for (const Placed& p : best.run.sheets[w].placed) subset.push_back(p.inst);
        std::sort(subset.begin(), subset.end());
        const std::size_t runs = std::min<std::size_t>(static_cast<std::size_t>(budget / 3), strategies.size());
        const std::size_t stride = std::max<std::size_t>(1, strategies.size() / runs);
        for (std::size_t r = 0; r < runs && subset.size() > 1; ++r) {
            if (shouldStop(true)) break;
            const Strategy& s = strategies[(r * stride) % strategies.size()];
            PackResult single = pack(job, instances, sortedOrder(instances, subset, s.sort), s,
                                     best.run.sheets[w].stock, 1);
            ++improvementRuns;
            if (!single.unplaced.empty() || single.sheets.size() != 1) continue;
            PackResult candidate = best.run;
            candidate.sheets[w] = std::move(single.sheets[0]);
            const Strategy keep = best.strategy;
            const std::vector<int> keepOrder = best.order;
            evaluate(std::move(candidate), keep, keepOrder, best, true);
        }
    }

    // Assemble the public result.
    CuttingResult result;
    result.jobId = job.id;
    for (const PackedSheet& sheet : best.run.sheets) result.sheets.push_back(buildSheetLayout(job, instances, sheet));
    std::vector<int> exhausted(job.pieces.size(), 0);
    for (int idx : best.run.unplaced) ++exhausted[static_cast<std::size_t>(instances[static_cast<std::size_t>(idx)].piece)];
    for (std::size_t pi = 0; pi < job.pieces.size(); ++pi) {
        const Piece& p = job.pieces[pi];
        if (noFit[pi] > 0)
            result.unplaced.push_back({p.id, noFit[pi], UnplacedReason::DoesNotFitAnyStock, noFitMessage(job, p)});
        else if (exhausted[pi] > 0)
            result.unplaced.push_back(
                {p.id, exhausted[pi], UnplacedReason::StockExhausted, exhaustedMessage(job, p, exhausted[pi])});
    }
    fillTotals(job, result);
    result.strategy = describe(best.strategy) + (best.improved ? " + order/re-pack improvement" : "");
    result.strategiesTried = tried;
    result.improvementRuns = improvementRuns;
    result.cancelled = cancelled;
    result.timeBudgetExhausted = timedOut;
    result.sheetCountLowerBound = std::max(0, areaLowerBoundOnSheetCount(job, result.totals.usedArea));
    result.isSheetCountProvenMinimal =
        result.unplaced.empty() && static_cast<int>(result.sheets.size()) == result.sheetCountLowerBound;
    result.isProvenOptimal = false;
    return result;
}

} // namespace fos::cutting
