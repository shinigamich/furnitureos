// SPDX-License-Identifier: AGPL-3.0-only
// Independent plan checker: recomputes everything from the job; shares no code with the optimiser.
#include "fos/core/units.hpp"
#include "fos/linear/linear.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace fos::linear {
namespace {

constexpr double kTol = 1e-6; // mm

std::string mm(double v) {
    return core::formatLength(v, 4) + " mm";
}

bool near(double a, double b, double tol = kTol) {
    return std::abs(a - b) <= tol * std::max(1.0, std::max(std::abs(a), std::abs(b)) * 1e-3);
}

} // namespace

core::Diagnostics validateLinearPlan(const LinearJob& job, const LinearResult& result) {
    core::Diagnostics out;
    auto fail = [&](std::string code, std::string message, std::string entity = {}) {
        out.push_back(core::Diagnostic{core::Severity::Error, "linear.validate." + std::move(code), std::move(message),
                                       std::move(entity)});
    };
    if (auto ok = validateJob(job); !ok) {
        fail("invalid_job", ok.error().message);
        return out;
    }
    std::map<std::string, const Stock*> stockById;
    for (const auto& s : job.stock) stockById[s.id] = &s;
    std::map<std::string, const Piece*> pieceById;
    for (const auto& p : job.pieces) pieceById[p.id] = &p;

    std::map<std::string, int> barsPerStock;
    std::set<std::pair<std::string, int>> seen;
    auto claim = [&](const std::string& id, int instance, double length, const std::string& where) {
        auto it = pieceById.find(id);
        if (it == pieceById.end()) {
            fail("unknown_piece", where + " refers to unknown piece '" + id + "'.", id);
            return;
        }
        if (instance < 0 || instance >= it->second->quantity)
            fail("bad_instance", where + ": instance " + std::to_string(instance) + " of piece '" + id +
                                     "' is outside 0.." + std::to_string(it->second->quantity - 1) + ".", id);
        if (!seen.insert({id, instance}).second)
            fail("duplicate_instance", where + ": instance " + std::to_string(instance) + " of piece '" + id +
                                           "' appears more than once.", id);
        if (!near(length, it->second->length))
            fail("wrong_length", where + ": piece '" + id + "' is " + mm(length) + " but the job says " +
                                     mm(it->second->length) + ".", id);
    };

    double totStock = 0, totPieces = 0, totKerf = 0, totReusable = 0, totCost = 0;
    int placed = 0;
    for (std::size_t b = 0; b < result.bars.size(); ++b) {
        const auto& bar = result.bars[b];
        const std::string where = "Bar " + std::to_string(b + 1) + " ('" + bar.stockId + "')";
        auto st = stockById.find(bar.stockId);
        if (st == stockById.end()) {
            fail("unknown_stock", where + " uses stock that is not in the job.", bar.stockId);
            continue;
        }
        const Stock& stock = *st->second;
        ++barsPerStock[stock.id];
        if (!near(bar.stockLength, stock.length))
            fail("stock_length", where + " is " + mm(bar.stockLength) + " long but the stock is " + mm(stock.length) +
                                     ".", stock.id);
        if (!near(bar.trimStart, job.trimStart) || !near(bar.trimEnd, job.trimEnd))
            fail("trim", where + " has trims " + mm(bar.trimStart) + " / " + mm(bar.trimEnd) + " instead of " +
                             mm(job.trimStart) + " / " + mm(job.trimEnd) + ".", stock.id);
        const double usableEnd = stock.length - job.trimEnd;
        double expectedStart = job.trimStart, pieces = 0;
        for (std::size_t c = 0; c < bar.cuts.size(); ++c) {
            const auto& cut = bar.cuts[c];
            const std::string cw = where + " cut " + std::to_string(c + 1);
            claim(cut.pieceId, cut.instance, cut.length, cw);
            if (c == 0 && !near(cut.start, job.trimStart))
                fail("first_position", cw + " starts at " + mm(cut.start) + "; the first piece must start at the trim (" +
                                           mm(job.trimStart) + ").", cut.pieceId);
            if (cut.start < expectedStart - kTol)
                fail("overlap", cw + " starts at " + mm(cut.start) + ", before " + mm(expectedStart) +
                                    " (previous piece end + kerf " + mm(job.kerf) + ").", cut.pieceId);
            if (cut.start + cut.length > usableEnd + kTol)
                fail("past_end", cw + " ends at " + mm(cut.start + cut.length) + ", beyond the usable end " +
                                     mm(usableEnd) + ".", cut.pieceId);
            expectedStart = cut.start + cut.length + job.kerf;
            pieces += cut.length;
        }
        placed += static_cast<int>(bar.cuts.size());
        // Recompute kerf and offcut from the positions (pieces packed tight: gap == kerf).
        const double lastEnd = bar.cuts.empty() ? job.trimStart : bar.cuts.back().start + bar.cuts.back().length;
        const double rem = std::max(0.0, usableEnd - lastEnd);
        int kerfCount = bar.cuts.empty() ? 0 : static_cast<int>(bar.cuts.size()) - 1;
        double kerfLoss = kerfCount * job.kerf;
        if (rem > kTol) {
            ++kerfCount;
            kerfLoss += std::min(job.kerf, rem);
        }
        const double offcut = rem > kTol ? std::max(0.0, rem - job.kerf) : 0.0;
        if (bar.kerfCount != kerfCount || !near(bar.kerfLoss, kerfLoss))
            fail("kerf", where + " reports " + std::to_string(bar.kerfCount) + " cuts / " + mm(bar.kerfLoss) +
                             " kerf; recomputed " + std::to_string(kerfCount) + " / " + mm(kerfLoss) + ".", stock.id);
        if (!near(bar.offcutLength, offcut) || (offcut > kTol && !near(bar.offcutStart, usableEnd - offcut)))
            fail("offcut", where + " reports an offcut of " + mm(bar.offcutLength) + " at " + mm(bar.offcutStart) +
                               "; recomputed " + mm(offcut) + " at " + mm(usableEnd - offcut) + ".", stock.id);
        const bool reusable = job.minReusableOffcut > 0 && offcut >= job.minReusableOffcut - kTol;
        if (bar.offcutReusable != reusable)
            fail("offcut_reusable", where + " marks its " + mm(offcut) + " offcut as " +
                                        (bar.offcutReusable ? "reusable" : "waste") + " (minimum reusable " +
                                        mm(job.minReusableOffcut) + ").", stock.id);
        if (!near(bar.pieceLength, pieces) || !near(bar.utilisation, pieces / stock.length, 1e-9))
            fail("utilisation", where + " reports piece length " + mm(bar.pieceLength) + " / utilisation " +
                                    core::formatLength(bar.utilisation, 6) + "; recomputed " + mm(pieces) + " / " +
                                    core::formatLength(pieces / stock.length, 6) + ".", stock.id);
        totStock += stock.length;
        totPieces += pieces;
        totKerf += kerfLoss;
        if (reusable) totReusable += offcut;
        totCost += stock.cost;
    }
    for (const auto& [id, n] : barsPerStock) {
        const Stock& s = *stockById[id];
        if (!s.unlimited && n > s.quantity)
            fail("stock_exceeded", "Stock '" + id + "' is used " + std::to_string(n) + " times but only " +
                                       std::to_string(s.quantity) + " are available.", id);
    }
    for (const auto& u : result.unplaced) {
        claim(u.pieceId, u.instance, u.length, "Unplaced entry");
        auto it = pieceById.find(u.pieceId);
        if (it == pieceById.end()) continue;
        double longest = 0;
        for (const auto& s : job.stock) longest = std::max(longest, usableLength(job, s.length));
        const bool tooLong = u.length > longest + kTol;
        if (tooLong != (u.reason == UnplacedReason::LongerThanEveryStock))
            fail("unplaced_reason", "Piece '" + u.pieceId + "' (" + mm(u.length) + ") is reported as " +
                                        toString(u.reason) + " but the longest usable stock is " + mm(longest) + ".",
                 u.pieceId);
    }
    for (const auto& p : job.pieces)
        for (int q = 0; q < p.quantity; ++q)
            if (!seen.count({p.id, q}))
                fail("missing_instance", "Instance " + std::to_string(q) + " of piece '" + p.id +
                                             "' is neither placed nor listed as unplaced.", p.id);
    int required = 0;
    for (const auto& p : job.pieces) required += p.quantity;
    if (result.requiredCount != required || result.placedCount != placed)
        fail("counts", "Reported " + std::to_string(result.placedCount) + " of " + std::to_string(result.requiredCount) +
                           " placed; recomputed " + std::to_string(placed) + " of " + std::to_string(required) + ".");
    if (!near(result.totalStockLength, totStock) || !near(result.totalPieceLength, totPieces) ||
        !near(result.totalKerfLoss, totKerf) || !near(result.totalReusableOffcut, totReusable) ||
        !near(result.totalWaste, totStock - totPieces - totReusable) || !near(result.totalCost, totCost) ||
        !near(result.utilisation, totStock > 0 ? totPieces / totStock : 0.0, 1e-9))
        fail("totals", "Reported totals (stock " + mm(result.totalStockLength) + ", pieces " +
                           mm(result.totalPieceLength) + ", waste " + mm(result.totalWaste) +
                           ") do not match the recomputed ones (stock " + mm(totStock) + ", pieces " + mm(totPieces) +
                           ", waste " + mm(totStock - totPieces - totReusable) + ").");
    if (result.isBarCountProvenMinimal &&
        (!result.unplaced.empty() || static_cast<int>(result.bars.size()) != result.barCountLowerBound))
        fail("proven_minimal", "The plan claims a proven minimal bar count but uses " +
                                   std::to_string(result.bars.size()) + " bars against a lower bound of " +
                                   std::to_string(result.barCountLowerBound) + ".");
    return out;
}

} // namespace fos::linear
