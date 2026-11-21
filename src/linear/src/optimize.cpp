// SPDX-License-Identifier: AGPL-3.0-only
// Heuristic 1D cutting-stock optimiser: first-fit / best-fit decreasing over several piece
// orders (deterministic + seeded perturbations), then a bounded "empty the weakest bar"
// improvement and a pass that moves each bar to the shortest suitable stock type.
#include "fos/core/units.hpp"
#include "fos/linear/linear.hpp"

#include <algorithm>
#include <climits>
#include <cmath>
#include <numeric>
#include <random>
#include <tuple>

namespace fos::linear {
namespace {

constexpr double kEps = 1e-6; // mm

struct Inst {
    int piece = 0;
    int instance = 0;
    double length = 0;
};

struct WorkBar {
    int stock = 0;
    double used = 0; ///< sum(length) + (n-1) kerf, measured from the usable start.
    std::vector<int> insts;
};

struct Solution {
    std::vector<WorkBar> bars;
    std::vector<int> unplaced; ///< Instance indices.
};

class Packer {
public:
    Packer(const LinearJob& job, std::vector<Inst> insts) : job_(job), insts_(std::move(insts)) {
        for (const auto& s : job.stock) {
            usable_.push_back(usableLength(job, s.length));
            avail_.push_back(s.unlimited ? INT_MAX : s.quantity);
        }
        openOrder_.resize(job.stock.size());
        std::iota(openOrder_.begin(), openOrder_.end(), 0);
        const bool longFirst = job.objective == Objective::MinimizeBars;
        const bool offcutsFirst = job.objective == Objective::PreferOffcuts;
        std::stable_sort(openOrder_.begin(), openOrder_.end(), [&](int a, int b) {
            const auto& sa = job.stock[static_cast<std::size_t>(a)];
            const auto& sb = job.stock[static_cast<std::size_t>(b)];
            if (sa.priority != sb.priority) return sa.priority < sb.priority;
            if (offcutsFirst && sa.isOffcut != sb.isOffcut) return sa.isOffcut;
            return longFirst ? sa.length > sb.length : sa.length < sb.length;
        });
        downsizeOrder_ = openOrder_;
        std::stable_sort(downsizeOrder_.begin(), downsizeOrder_.end(), [&](int a, int b) {
            const auto& sa = job.stock[static_cast<std::size_t>(a)];
            const auto& sb = job.stock[static_cast<std::size_t>(b)];
            if (sa.priority != sb.priority) return sa.priority < sb.priority;
            if (offcutsFirst && sa.isOffcut != sb.isOffcut) return sa.isOffcut;
            return sa.length < sb.length;
        });
    }

    double usable(int stock) const { return usable_[static_cast<std::size_t>(stock)]; }
    const std::vector<Inst>& insts() const { return insts_; }

    Solution build(const std::vector<int>& order, bool bestFit) const {
        Solution sol;
        std::vector<int> avail = avail_;
        for (int i : order) {
            const double len = insts_[static_cast<std::size_t>(i)].length;
            int target = -1;
            double bestSlack = 0;
            for (std::size_t b = 0; b < sol.bars.size(); ++b) {
                const double slack = slackAfter(sol.bars[b], len);
                if (slack < -kEps) continue;
                if (!bestFit) {
                    target = static_cast<int>(b);
                    break;
                }
                if (target < 0 || slack < bestSlack - kEps) {
                    target = static_cast<int>(b);
                    bestSlack = slack;
                }
            }
            if (target < 0) {
                for (int s : openOrder_) {
                    if (avail[static_cast<std::size_t>(s)] <= 0 || usable(s) < len - kEps) continue;
                    if (avail[static_cast<std::size_t>(s)] != INT_MAX) --avail[static_cast<std::size_t>(s)];
                    sol.bars.push_back(WorkBar{s, 0, {}});
                    target = static_cast<int>(sol.bars.size() - 1);
                    break;
                }
            }
            if (target < 0) {
                sol.unplaced.push_back(i);
                continue;
            }
            add(sol.bars[static_cast<std::size_t>(target)], i);
        }
        improve(sol);
        downsize(sol);
        return sol;
    }

private:
    double slackAfter(const WorkBar& b, double len) const {
        const double need = b.insts.empty() ? len : b.used + job_.kerf + len;
        return usable(b.stock) - need;
    }
    void add(WorkBar& b, int i) const {
        const double len = insts_[static_cast<std::size_t>(i)].length;
        b.used = b.insts.empty() ? len : b.used + job_.kerf + len;
        b.insts.push_back(i);
    }
    double content(const WorkBar& b) const {
        double s = 0;
        for (int i : b.insts) s += insts_[static_cast<std::size_t>(i)].length;
        return s;
    }

    /// Bounded improvement: repeatedly try to dissolve the bar with the least piece length into
    /// the free space of the other bars (best fit, longest piece first). Stops at the first
    /// failure; at most bars.size() rounds.
    void improve(Solution& sol) const {
        const std::size_t rounds = sol.bars.size();
        for (std::size_t r = 0; r < rounds && sol.bars.size() > 1; ++r) {
            std::size_t weakest = 0;
            for (std::size_t b = 1; b < sol.bars.size(); ++b)
                if (content(sol.bars[b]) < content(sol.bars[weakest]) - kEps) weakest = b;
            std::vector<WorkBar> trial = sol.bars;
            std::vector<int> moving = trial[weakest].insts;
            trial.erase(trial.begin() + static_cast<std::ptrdiff_t>(weakest));
            std::stable_sort(moving.begin(), moving.end(), [&](int a, int b) {
                return insts_[static_cast<std::size_t>(a)].length > insts_[static_cast<std::size_t>(b)].length;
            });
            bool ok = true;
            for (int i : moving) {
                const double len = insts_[static_cast<std::size_t>(i)].length;
                int target = -1;
                double bestSlack = 0;
                for (std::size_t b = 0; b < trial.size(); ++b) {
                    const double slack = slackAfter(trial[b], len);
                    if (slack >= -kEps && (target < 0 || slack < bestSlack - kEps)) {
                        target = static_cast<int>(b);
                        bestSlack = slack;
                    }
                }
                if (target < 0) {
                    ok = false;
                    break;
                }
                add(trial[static_cast<std::size_t>(target)], i);
            }
            if (!ok) return;
            sol.bars = std::move(trial);
        }
    }

    /// Moves each bar (fullest first) to the first stock type in downsize order that holds its
    /// content and is still available (a bar's own type counts as available to itself).
    void downsize(Solution& sol) const {
        std::vector<int> avail = avail_;
        for (const auto& b : sol.bars)
            if (avail[static_cast<std::size_t>(b.stock)] != INT_MAX) --avail[static_cast<std::size_t>(b.stock)];
        std::vector<std::size_t> idx(sol.bars.size());
        std::iota(idx.begin(), idx.end(), std::size_t{0});
        std::stable_sort(idx.begin(), idx.end(), [&](std::size_t a, std::size_t b) {
            return sol.bars[a].used > sol.bars[b].used;
        });
        for (std::size_t k : idx) {
            auto& bar = sol.bars[k];
            auto& own = avail[static_cast<std::size_t>(bar.stock)];
            if (own != INT_MAX) ++own;
            for (int s : downsizeOrder_) {
                if (avail[static_cast<std::size_t>(s)] <= 0 || usable(s) < bar.used - kEps) continue;
                bar.stock = s;
                break;
            }
            auto& now = avail[static_cast<std::size_t>(bar.stock)];
            if (now != INT_MAX) --now;
        }
    }

    const LinearJob& job_;
    std::vector<Inst> insts_;
    std::vector<double> usable_;
    std::vector<int> avail_;
    std::vector<int> openOrder_, downsizeOrder_;
};

struct Score {
    std::size_t unplaced = 0;
    std::size_t bars = 0;
    double waste = 0;    ///< stock - pieces - reusable offcut
    double newStock = 0; ///< length of non-offcut bars
    double cost = 0;
};

BarPlan finalizeBar(const LinearJob& job, const Packer& packer, const WorkBar& wb) {
    const auto& stock = job.stock[static_cast<std::size_t>(wb.stock)];
    BarPlan bar;
    bar.stockId = stock.id;
    bar.stockLength = stock.length;
    bar.isOffcut = stock.isOffcut;
    bar.trimStart = job.trimStart;
    bar.trimEnd = job.trimEnd;
    std::vector<int> order = wb.insts;
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
        const auto& ia = packer.insts()[static_cast<std::size_t>(a)];
        const auto& ib = packer.insts()[static_cast<std::size_t>(b)];
        if (std::abs(ia.length - ib.length) > kEps) return ia.length > ib.length;
        return std::tie(ia.piece, ia.instance) < std::tie(ib.piece, ib.instance);
    });
    double pos = job.trimStart;
    for (std::size_t k = 0; k < order.size(); ++k) {
        const auto& in = packer.insts()[static_cast<std::size_t>(order[k])];
        if (k > 0) pos += job.kerf;
        bar.cuts.push_back(Cut{job.pieces[static_cast<std::size_t>(in.piece)].id, in.instance, pos, in.length});
        pos += in.length;
        bar.pieceLength += in.length;
    }
    const double usableEnd = stock.length - job.trimEnd;
    const double rem = std::max(0.0, usableEnd - pos);
    bar.kerfCount = order.empty() ? 0 : static_cast<int>(order.size()) - 1;
    bar.kerfLoss = bar.kerfCount * job.kerf;
    if (rem > kEps) {
        ++bar.kerfCount;
        bar.kerfLoss += std::min(job.kerf, rem);
    }
    bar.offcutLength = rem > kEps ? std::max(0.0, rem - job.kerf) : 0.0;
    bar.offcutStart = usableEnd - bar.offcutLength;
    bar.offcutReusable = job.minReusableOffcut > 0 && bar.offcutLength >= job.minReusableOffcut - kEps;
    bar.utilisation = bar.pieceLength / stock.length;
    return bar;
}

Score score(const LinearJob& job, const Packer& packer, const Solution& sol) {
    Score s;
    s.unplaced = sol.unplaced.size();
    s.bars = sol.bars.size();
    for (const auto& wb : sol.bars) {
        const auto bar = finalizeBar(job, packer, wb);
        const auto& st = job.stock[static_cast<std::size_t>(wb.stock)];
        s.waste += bar.stockLength - bar.pieceLength - (bar.offcutReusable ? bar.offcutLength : 0.0);
        if (!st.isOffcut) s.newStock += st.length;
        s.cost += st.cost;
    }
    return s;
}

/// Negative when a is better than b, positive when worse, 0 when equal within tolerance.
int compare(Objective o, const Score& a, const Score& b) {
    auto cmpD = [](double x, double y) { return x < y - kEps ? -1 : (x > y + kEps ? 1 : 0); };
    auto cmpZ = [](std::size_t x, std::size_t y) { return x < y ? -1 : (x > y ? 1 : 0); };
    std::vector<int> keys{cmpZ(a.unplaced, b.unplaced)};
    switch (o) {
    case Objective::MinimizeBars:
        keys.insert(keys.end(), {cmpZ(a.bars, b.bars), cmpD(a.waste, b.waste), cmpD(a.cost, b.cost)});
        break;
    case Objective::MinimizeWaste:
        keys.insert(keys.end(), {cmpD(a.waste, b.waste), cmpZ(a.bars, b.bars), cmpD(a.cost, b.cost)});
        break;
    case Objective::PreferOffcuts:
        keys.insert(keys.end(),
                    {cmpD(a.newStock, b.newStock), cmpZ(a.bars, b.bars), cmpD(a.waste, b.waste), cmpD(a.cost, b.cost)});
        break;
    }
    for (int k : keys)
        if (k != 0) return k;
    return 0;
}

std::string mm(double v) {
    return core::formatLength(v) + " mm";
}

} // namespace

core::Result<LinearResult> optimizeLinear(const LinearJob& job) {
    if (auto ok = validateJob(job); !ok) return ok.error();

    std::vector<Inst> insts;
    for (std::size_t p = 0; p < job.pieces.size(); ++p)
        for (int q = 0; q < job.pieces[p].quantity; ++q)
            insts.push_back(Inst{static_cast<int>(p), q, job.pieces[p].length});

    LinearResult res;
    res.requiredCount = static_cast<int>(insts.size());

    double maxUsableAny = 0, maxUsableAvail = 0;
    for (const auto& s : job.stock) {
        const double u = usableLength(job, s.length);
        maxUsableAny = std::max(maxUsableAny, u);
        if (s.unlimited || s.quantity > 0) maxUsableAvail = std::max(maxUsableAvail, u);
    }
    // Instances longer than every stock type never enter the packer.
    std::vector<Inst> packable;
    std::vector<Inst> tooLong;
    for (const auto& in : insts) (in.length > maxUsableAny + kEps ? tooLong : packable).push_back(in);

    double lbSum = 0;
    for (const auto& in : packable) lbSum += in.length + job.kerf;
    if (!packable.empty() && maxUsableAvail > 0)
        res.barCountLowerBound = static_cast<int>(std::ceil(lbSum / (maxUsableAvail + job.kerf) - 1e-9));

    Packer packer(job, packable);
    std::vector<int> base(packable.size());
    std::iota(base.begin(), base.end(), 0);
    std::stable_sort(base.begin(), base.end(), [&](int a, int b) {
        return packable[static_cast<std::size_t>(a)].length > packable[static_cast<std::size_t>(b)].length + kEps;
    });

    Solution best;
    Score bestScore;
    bool have = false;
    auto consider = [&](const std::vector<int>& order, bool bestFit) {
        Solution sol = packer.build(order, bestFit);
        const Score sc = score(job, packer, sol);
        ++res.ordersTried;
        if (!have || compare(job.objective, sc, bestScore) < 0) {
            best = std::move(sol);
            bestScore = sc;
            have = true;
        }
    };
    consider(base, false); // first-fit decreasing
    consider(base, true);  // best-fit decreasing

    // Seeded perturbations of the decreasing order (portable: mt19937_64 output is specified
    // by the standard; no std::shuffle / distributions, whose results differ between libraries).
    const int iterations = packable.size() > 5000 ? std::min(job.iterations, 4) : job.iterations;
    std::mt19937_64 rng(job.seed);
    for (int it = 0; it < iterations; ++it) {
        if (job.objective == Objective::MinimizeBars && bestScore.unplaced == 0 &&
            static_cast<int>(bestScore.bars) <= res.barCountLowerBound)
            break; // proven minimal bar count; nothing to gain for this objective
        std::vector<std::pair<double, int>> keyed;
        keyed.reserve(packable.size());
        for (int i : base) {
            const double noise = static_cast<double>(rng() >> 11) * 0x1.0p-53; // [0,1)
            keyed.emplace_back(packable[static_cast<std::size_t>(i)].length * (1.0 + 0.35 * noise), i);
        }
        std::stable_sort(keyed.begin(), keyed.end(),
                         [](const auto& a, const auto& b) { return a.first > b.first; });
        std::vector<int> order;
        order.reserve(keyed.size());
        for (const auto& k : keyed) order.push_back(k.second);
        consider(order, it % 2 == 1);
    }

    // Output: bars by decreasing piece length (ties: stock order, then first instance).
    for (const auto& wb : best.bars) res.bars.push_back(finalizeBar(job, packer, wb));
    std::stable_sort(res.bars.begin(), res.bars.end(), [](const BarPlan& a, const BarPlan& b) {
        return a.pieceLength > b.pieceLength + kEps;
    });

    for (const auto& in : tooLong) {
        const auto& p = job.pieces[static_cast<std::size_t>(in.piece)];
        res.unplaced.push_back(Unplaced{p.id, in.instance, in.length, UnplacedReason::LongerThanEveryStock,
                                        "Piece '" + p.id + "' (" + mm(in.length) +
                                            ") is longer than the usable length of every stock (longest " +
                                            mm(maxUsableAny) + " after trims)."});
    }
    for (int i : best.unplaced) {
        const auto& in = packable[static_cast<std::size_t>(i)];
        const auto& p = job.pieces[static_cast<std::size_t>(in.piece)];
        res.unplaced.push_back(Unplaced{p.id, in.instance, in.length, UnplacedReason::StockExhausted,
                                        "Piece '" + p.id + "' (" + mm(in.length) +
                                            ") could not be placed: every stock bar long enough is used up."});
    }
    std::stable_sort(res.unplaced.begin(), res.unplaced.end(), [&](const Unplaced& a, const Unplaced& b) {
        return std::tie(a.pieceId, a.instance) < std::tie(b.pieceId, b.instance);
    });

    for (const auto& b : res.bars) {
        res.placedCount += static_cast<int>(b.cuts.size());
        res.totalStockLength += b.stockLength;
        res.totalPieceLength += b.pieceLength;
        res.totalKerfLoss += b.kerfLoss;
        res.totalTrim += b.trimStart + b.trimEnd;
        if (b.offcutReusable) res.totalReusableOffcut += b.offcutLength;
        for (const auto& s : job.stock)
            if (s.id == b.stockId) res.totalCost += s.cost;
    }
    res.totalWaste = res.totalStockLength - res.totalPieceLength - res.totalReusableOffcut;
    res.utilisation = res.totalStockLength > 0 ? res.totalPieceLength / res.totalStockLength : 0.0;
    res.isBarCountProvenMinimal =
        res.unplaced.empty() && static_cast<int>(res.bars.size()) == res.barCountLowerBound;
    for (const auto& u : res.unplaced)
        res.diagnostics.push_back(core::Diagnostic{core::Severity::Warning,
                                                   u.reason == UnplacedReason::LongerThanEveryStock
                                                       ? "linear.piece.too_long"
                                                       : "linear.stock.exhausted",
                                                   u.message, u.pieceId});
    return res;
}

} // namespace fos::linear
