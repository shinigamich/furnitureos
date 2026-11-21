// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/core/units.hpp"
#include "fos/linear/linear.hpp"

#include <cmath>
#include <set>

namespace fos::linear {
namespace {

std::string mm(double v) {
    return core::formatLength(v) + " mm";
}

core::Error bad(std::string message, std::string context = {}) {
    return core::makeError(core::ErrorCode::InvalidArgument, std::move(message), std::move(context));
}

bool nonNegativeFinite(double v) noexcept {
    return std::isfinite(v) && v >= 0.0;
}

} // namespace

const char* toString(Objective o) noexcept {
    switch (o) {
    case Objective::MinimizeBars: return "minimize-bars";
    case Objective::MinimizeWaste: return "minimize-waste";
    case Objective::PreferOffcuts: return "prefer-offcuts";
    }
    return "?";
}

const char* toString(UnplacedReason r) noexcept {
    switch (r) {
    case UnplacedReason::LongerThanEveryStock: return "longer-than-every-stock";
    case UnplacedReason::StockExhausted: return "stock-exhausted";
    }
    return "?";
}

double usableLength(const LinearJob& job, double stockLength) noexcept {
    return stockLength - job.trimStart - job.trimEnd;
}

core::Result<void> validateJob(const LinearJob& job) {
    if (job.pieces.empty()) return bad("The linear cutting job has no pieces.");
    if (job.stock.empty()) return bad("The linear cutting job has no stock lengths.");
    if (!nonNegativeFinite(job.kerf)) return bad("Kerf " + mm(job.kerf) + " must be a finite number >= 0.");
    if (!nonNegativeFinite(job.trimStart) || !nonNegativeFinite(job.trimEnd))
        return bad("Trims (start " + mm(job.trimStart) + ", end " + mm(job.trimEnd) +
                   ") must be finite numbers >= 0.");
    if (!nonNegativeFinite(job.minReusableOffcut))
        return bad("Minimum reusable offcut " + mm(job.minReusableOffcut) + " must be a finite number >= 0.");
    if (job.iterations < 0) return bad("Iterations " + std::to_string(job.iterations) + " must be >= 0.");
    std::set<std::string> ids;
    long long instances = 0;
    for (const auto& p : job.pieces) {
        if (p.id.empty()) return bad("A piece has an empty id.");
        if (!ids.insert(p.id).second) return bad("Piece id '" + p.id + "' is used twice.", p.id);
        if (!std::isfinite(p.length) || p.length <= 0)
            return bad("Piece '" + p.id + "' has length " + mm(p.length) + "; it must be > 0.", p.id);
        if (p.quantity < 1)
            return bad("Piece '" + p.id + "' has quantity " + std::to_string(p.quantity) + "; it must be >= 1.",
                       p.id);
        instances += p.quantity;
    }
    if (instances > 200000)
        return bad("The job has " + std::to_string(instances) + " piece instances; at most 200000 are supported.");
    ids.clear();
    for (const auto& s : job.stock) {
        if (s.id.empty()) return bad("A stock entry has an empty id.");
        if (!ids.insert(s.id).second) return bad("Stock id '" + s.id + "' is used twice.", s.id);
        if (!std::isfinite(s.length) || usableLength(job, s.length) <= 0)
            return bad("Stock '" + s.id + "' is " + mm(s.length) + " long, which does not exceed the trims (" +
                           mm(job.trimStart) + " + " + mm(job.trimEnd) + ").",
                       s.id);
        if (!s.unlimited && s.quantity < 0)
            return bad("Stock '" + s.id + "' has quantity " + std::to_string(s.quantity) + "; it must be >= 0.",
                       s.id);
        if (!nonNegativeFinite(s.cost))
            return bad("Stock '" + s.id + "' has cost " + core::formatLength(s.cost) + "; it must be >= 0.", s.id);
    }
    return {};
}

} // namespace fos::linear
