// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/nesting/nest.hpp"

#include "placer.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <numeric>

namespace fos::nesting {

namespace {

using namespace detail;

/// 32-bit linear congruential generator (Numerical Recipes constants); identical on every platform.
class Lcg {
public:
    explicit Lcg(std::uint32_t seed) : state_(seed) {}
    std::uint32_t next() {
        state_ = state_ * 1664525u + 1013904223u;
        return state_;
    }
    /// Uniform-ish integer in [0, n), n >= 1 (high bits, modulo; bias irrelevant here).
    std::size_t below(std::size_t n) { return static_cast<std::size_t>((next() >> 8) % static_cast<std::uint32_t>(n)); }

private:
    std::uint32_t state_;
};

std::string fmt(double v) { return core::formatLength(v, 1); }

/// Instances sorted by a per-part key (descending), ties by part index then instance.
std::vector<Instance> orderBy(const Job& job, const std::vector<double>& key) {
    std::vector<int> idx(job.parts.size());
    std::iota(idx.begin(), idx.end(), 0);
    std::stable_sort(idx.begin(), idx.end(), [&](int a, int b) {
        return key[static_cast<std::size_t>(a)] > key[static_cast<std::size_t>(b)] + 1e-9;
    });
    std::vector<Instance> out;
    for (int p : idx)
        for (int k = 0; k < job.parts[static_cast<std::size_t>(p)].quantity; ++k) out.push_back({p, k});
    return out;
}

/// One seeded perturbation of an order: swap two instances of different parts, or move one
/// instance to another position. Returns false when the order cannot change (one part only).
bool perturb(std::vector<Instance>& order, Lcg& rng) {
    const std::size_t n = order.size();
    if (n < 2) return false;
    for (int attempt = 0; attempt < 16; ++attempt) {
        const std::size_t i = rng.below(n), j = rng.below(n);
        if (i == j || order[i].part == order[j].part) continue;
        if (rng.below(2) == 0) {
            std::swap(order[i], order[j]);
        } else {
            const Instance v = order[i];
            order.erase(order.begin() + static_cast<std::ptrdiff_t>(i));
            order.insert(order.begin() + static_cast<std::ptrdiff_t>(j), v);
        }
        return true;
    }
    return false;
}

std::string tooLargeMessage(const Job& job, const Part& part, int instance, const Shape& shape) {
    std::string m = "Part '" + part.label + "' (" + part.id + ", instance " + std::to_string(instance) +
                    ") does not fit any sheet in any allowed orientation: its bounding box is ";
    const std::size_t shown = std::min<std::size_t>(shape.orients.size(), 8);
    for (std::size_t i = 0; i < shown; ++i) {
        const auto& o = shape.orients[i];
        if (i) m += ", ";
        m += fmt(o.w) + " x " + fmt(o.h) + " mm at " + fmt(o.rotationDeg) + " deg" + (o.mirrored ? " mirrored" : "");
    }
    if (shown < shape.orients.size()) m += ", ...";
    m += "; placement areas:";
    for (std::size_t t = 0; t < job.sheets.size(); ++t) {
        const auto a = placementArea(job, job.sheets[t]);
        m += (t ? ", " : " ") + std::string("'") + job.sheets[t].id + "' " + fmt(a.width()) + " x " +
             fmt(a.height()) + " mm";
    }
    return m + ".";
}

NestResult assemble(const Job& job, const std::vector<Shape>& shapes, const Placer& placer, const Decoded& d) {
    NestResult r;
    std::vector<int> copies(job.sheets.size(), 0);
    for (const OpenSheet& os : d.sheets) {
        const Sheet& sheet = job.sheets[static_cast<std::size_t>(os.stock)];
        SheetLayout L;
        L.sheetId = sheet.id;
        L.copy = copies[static_cast<std::size_t>(os.stock)]++;
        L.sheetArea = sheet.length * sheet.width;
        for (const PlacedItem& it : os.items) {
            const auto& o = shapes[static_cast<std::size_t>(it.part)].orients[static_cast<std::size_t>(it.orient)];
            Placement p;
            p.partId = job.parts[static_cast<std::size_t>(it.part)].id;
            p.instance = it.inst.instance;
            p.rotationDeg = o.rotationDeg;
            p.mirrored = o.mirrored;
            p.translation = it.pos - o.offset;
            p.outline = it.outline;
            L.usedLength = std::max(L.usedLength, it.box.max.x);
            L.partArea += shapes[static_cast<std::size_t>(it.part)].area;
            L.netPartArea += shapes[static_cast<std::size_t>(it.part)].netArea;
            L.placements.push_back(std::move(p));
        }
        const double x0 = L.usedLength + job.partSpacing;
        if (sheet.length - x0 > core::kLengthEps && sheet.length - x0 >= job.minRemnantLength) {
            L.hasRemnant = true;
            L.remnant = geometry::Rect2{{x0, 0}, {sheet.length, sheet.width}};
        }
        L.utilisation = L.sheetArea > 0 ? L.partArea / L.sheetArea : 0;
        L.netUtilisation = L.sheetArea > 0 ? L.netPartArea / L.sheetArea : 0;
        r.totalNetPartArea += L.netPartArea;
        r.placedCount += static_cast<int>(L.placements.size());
        r.totalPartArea += L.partArea;
        r.totalSheetArea += L.sheetArea;
        r.sheets.push_back(std::move(L));
    }
    std::vector<Instance> un = d.unplaced;
    std::sort(un.begin(), un.end(),
              [](const Instance& a, const Instance& b) { return a.part != b.part ? a.part < b.part : a.instance < b.instance; });
    for (const Instance& in : un) {
        const Part& part = job.parts[static_cast<std::size_t>(in.part)];
        Unplaced u;
        u.partId = part.id;
        u.instance = in.instance;
        const auto& fits = placer.fitsEmpty()[static_cast<std::size_t>(in.part)];
        if (std::find(fits.begin(), fits.end(), true) == fits.end()) {
            u.reason = UnplacedReason::TooLarge;
            u.message = tooLargeMessage(job, part, in.instance, shapes[static_cast<std::size_t>(in.part)]);
        } else {
            u.reason = UnplacedReason::StockExhausted;
            u.message = "Part '" + part.label + "' (" + part.id + ", instance " + std::to_string(in.instance) +
                        ") fits an empty sheet but no sheet with room for it is left in stock (" +
                        std::to_string(d.sheets.size()) + " sheets used).";
        }
        r.unplaced.push_back(std::move(u));
    }
    r.utilisation = r.totalSheetArea > 0 ? r.totalPartArea / r.totalSheetArea : 0;
    r.netUtilisation = r.totalSheetArea > 0 ? r.totalNetPartArea / r.totalSheetArea : 0;
    r.isHeuristic = true;
    return r;
}

} // namespace

core::Result<NestResult> nest(const Job& job, const NestOptions& options) {
    const auto diags = validateJob(job, options);
    for (const auto& dg : diags)
        if (dg.severity == core::Severity::Error)
            return core::makeError(core::ErrorCode::InvalidArgument, dg.message, dg.entityId);

    std::vector<Shape> shapes;
    std::vector<double> byArea, byLength, byBox, byPerimeter;
    for (const Part& p : job.parts) {
        shapes.push_back(buildShape(p, allowedRotations(p, options)));
        const auto bb = geometry::bounds(p.outline);
        byArea.push_back(shapes.back().area);
        byLength.push_back(std::max(bb.width(), bb.height()));
        byBox.push_back(bb.area());
        byPerimeter.push_back(geometry::perimeter(p.outline));
    }
    Placer placer(job, shapes, options);

    const auto t0 = std::chrono::steady_clock::now();
    int decodes = 0;
    bool cancelled = false;
    const auto budgetLeft = [&]() {
        if (options.maxDecodes > 0 && decodes >= options.maxDecodes) return false;
        if (options.timeBudgetMs > 0 &&
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() >=
                options.timeBudgetMs)
            return false;
        return true;
    };

    std::vector<Instance> bestOrder;
    PlacementScore bestPolicy = options.placementScores.front();
    Decoded best;
    Score bestScore;
    bool have = false;
    // Evaluates one (policy, order); returns false when cancelled or out of budget.
    const auto tryStart = [&](PlacementScore policy, std::vector<Instance> order) {
        if (have && !budgetLeft()) return false;
        Decoded d = placer.decode(order, policy);
        if (d.aborted) {
            cancelled = true;
            return false;
        }
        ++decodes;
        const Score sc = scoreOf(job, d);
        if (!have || sc.betterThan(bestScore)) {
            best = std::move(d);
            bestScore = sc;
            bestOrder = std::move(order);
            bestPolicy = policy;
            have = true;
        }
        return true;
    };
    const auto same = [](const std::vector<Instance>& a, const std::vector<Instance>& b) {
        return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](const Instance& x, const Instance& y) {
                   return x.part == y.part && x.instance == y.instance;
               });
    };

    // 1. Deterministic restarts: every placement score with every distinct starting order.
    std::vector<std::vector<Instance>> starts;
    for (InstanceOrder o : options.orders) {
        const std::vector<double>* key = &byArea;
        switch (o) {
        case InstanceOrder::AreaDesc: key = &byArea; break;
        case InstanceOrder::LongestSideDesc: key = &byLength; break;
        case InstanceOrder::BoxAreaDesc: key = &byBox; break;
        case InstanceOrder::PerimeterDesc: key = &byPerimeter; break;
        }
        std::vector<Instance> order = orderBy(job, *key);
        if (std::none_of(starts.begin(), starts.end(), [&](const auto& s) { return same(s, order); }))
            starts.push_back(std::move(order));
    }
    // Seeded shuffles (Fisher-Yates with a seed-derived LCG per restart).
    for (int k = 0; k < options.randomRestarts; ++k) {
        std::vector<Instance> order = starts.front();
        Lcg shuffle(options.seed ^ (0x9E3779B9u * static_cast<std::uint32_t>(k + 1)));
        for (std::size_t i = order.size(); i > 1; --i) std::swap(order[i - 1], order[shuffle.below(i)]);
        starts.push_back(std::move(order));
    }
    bool stop = false;
    for (std::size_t si = 0; si < starts.size() && !stop; ++si)
        for (PlacementScore policy : options.placementScores)
            if (!tryStart(policy, starts[si])) {
                stop = true;
                break;
            }
    if (!have)
        return core::makeError(core::ErrorCode::Cancelled, "Nesting was cancelled before the first complete layout.");

    // 2. Seeded improvement (hill climbing with plateau moves) from the best start, same policy.
    Lcg rng(options.seed);
    int iterations = 0;
    for (int it = 0; !cancelled && it < options.iterations && budgetLeft(); ++it) {
        std::vector<Instance> order = bestOrder;
        if (!perturb(order, rng)) break;
        Decoded d = placer.decode(order, bestPolicy);
        if (d.aborted) {
            cancelled = true;
            break;
        }
        ++iterations;
        ++decodes;
        const Score sc = scoreOf(job, d);
        if (!bestScore.betterThan(sc)) {
            best = std::move(d);
            bestScore = sc;
            bestOrder = std::move(order);
        }
    }
    NestResult r = assemble(job, shapes, placer, best);
    r.iterationsRun = iterations;
    r.decodesRun = decodes;
    r.placementScoreUsed = bestPolicy;
    r.cancelled = cancelled;
    // Area lower bound on sheets for all instances.
    double need = 0, cap = 0;
    for (std::size_t p = 0; p < job.parts.size(); ++p)
        need += job.parts[p].quantity * (options.partInPart ? shapes[p].netArea : shapes[p].area);
    for (const Sheet& sh : job.sheets) cap = std::max(cap, placementArea(job, sh).area());
    r.sheetLowerBound = cap > 0 ? static_cast<int>(std::ceil(need / cap - 1e-9)) : 0;
    return r;
}

} // namespace fos::nesting
