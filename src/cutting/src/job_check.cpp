// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/core/units.hpp"
#include "fos/cutting/optimizer.hpp"

#include <algorithm>
#include <cmath>
#include <set>

namespace fos::cutting {
namespace {

using core::formatLength;

void addError(core::Diagnostics& out, const char* code, std::string message, const std::string& entity) {
    out.push_back({core::Severity::Error, code, std::move(message), entity});
}

bool positive(double v) { return std::isfinite(v) && v > core::kLengthEps; }
bool nonNegative(double v) { return std::isfinite(v) && v >= 0.0; }

} // namespace

const char* toString(Objective o) noexcept {
    switch (o) {
    case Objective::MinimizeSheets: return "MinimizeSheets";
    case Objective::MinimizeWaste: return "MinimizeWaste";
    case Objective::PreferRemnants: return "PreferRemnants";
    case Objective::MinimizeCuts: return "MinimizeCuts";
    }
    return "Unknown";
}

const char* toString(CutOrientation o) noexcept {
    return o == CutOrientation::Lengthwise ? "Lengthwise" : "Crosswise";
}

const char* toString(FirstCut f) noexcept {
    switch (f) {
    case FirstCut::Free: return "Free";
    case FirstCut::Lengthwise: return "Lengthwise";
    case FirstCut::Crosswise: return "Crosswise";
    }
    return "Unknown";
}

const char* toString(UnplacedReason r) noexcept {
    return r == UnplacedReason::DoesNotFitAnyStock ? "DoesNotFitAnyStock" : "StockExhausted";
}

Rect usableArea(const StockSheet& stock, const Trim& trim) noexcept {
    return Rect{trim.left, trim.bottom, stock.length - trim.left - trim.right,
                stock.width - trim.bottom - trim.top};
}

bool isReusableRemnant(const Job& job, double length, double width) noexcept {
    const double longer = std::max(length, width);
    const double shorter = std::min(length, width);
    if (shorter <= core::kLengthEps) return false;
    return longer >= job.minRemnantLength - core::kLengthEps && shorter >= job.minRemnantWidth - core::kLengthEps;
}

core::Diagnostics validateJob(const Job& job) {
    core::Diagnostics out;
    const std::string jobName = "'" + job.id + "'";

    if (!nonNegative(job.kerf))
        addError(out, "cutting.job.invalid_kerf",
                 "Job " + jobName + " has an invalid saw kerf (" + std::to_string(job.kerf) +
                     " mm); it must be a finite value >= 0.",
                 job.id);
    if (!nonNegative(job.trim.left) || !nonNegative(job.trim.right) || !nonNegative(job.trim.top) ||
        !nonNegative(job.trim.bottom))
        addError(out, "cutting.job.invalid_trim",
                 "Job " + jobName + " has a negative or non-finite edge trim; every trim must be >= 0 mm.", job.id);
    if (!nonNegative(job.minRemnantLength) || !nonNegative(job.minRemnantWidth))
        addError(out, "cutting.job.invalid_remnant_limit",
                 "Job " + jobName + " has a negative or non-finite minimum remnant size.", job.id);
    if (job.maxStages != 0 && job.maxStages < 2)
        addError(out, "cutting.job.invalid_max_stages",
                 "Job " + jobName + " has maxStages = " + std::to_string(job.maxStages) +
                     "; use 0 for unlimited or a value of at least 2 (one cut per direction).",
                 job.id);

    std::set<std::string> ids;
    long long instances = 0;
    for (std::size_t i = 0; i < job.pieces.size(); ++i) {
        const Piece& p = job.pieces[i];
        const std::string name = "Piece '" + (p.label.empty() ? p.id : p.label) + "'";
        if (p.id.empty())
            addError(out, "cutting.job.piece_id_empty",
                     "Piece #" + std::to_string(i + 1) + " of job " + jobName + " has an empty id.", job.id);
        else if (!ids.insert(p.id).second)
            addError(out, "cutting.job.piece_id_duplicate",
                     "Piece id '" + p.id + "' occurs more than once in job " + jobName + ".", p.id);
        if (!positive(p.length) || !positive(p.width))
            addError(out, "cutting.job.piece_size_invalid",
                     name + " has an invalid size " + std::to_string(p.length) + " x " + std::to_string(p.width) +
                         " mm; both sides must be > 0.",
                     p.id);
        if (p.quantity < 1)
            addError(out, "cutting.job.piece_quantity_invalid",
                     name + " has quantity " + std::to_string(p.quantity) + "; it must be at least 1.", p.id);
        else
            instances += p.quantity;
    }
    if (instances > 100000)
        addError(out, "cutting.job.too_many_pieces",
                 "Job " + jobName + " asks for " + std::to_string(instances) +
                     " piece instances; the optimiser accepts at most 100000 per job.",
                 job.id);

    if (job.stock.empty())
        addError(out, "cutting.job.no_stock", "Job " + jobName + " has no stock sheets to cut from.", job.id);
    std::set<std::string> stockIds;
    for (std::size_t i = 0; i < job.stock.size(); ++i) {
        const StockSheet& s = job.stock[i];
        const std::string name = "Stock sheet '" + s.id + "'";
        if (s.id.empty())
            addError(out, "cutting.job.stock_id_empty",
                     "Stock sheet #" + std::to_string(i + 1) + " of job " + jobName + " has an empty id.", job.id);
        else if (!stockIds.insert(s.id).second)
            addError(out, "cutting.job.stock_id_duplicate",
                     "Stock sheet id '" + s.id + "' occurs more than once in job " + jobName + ".", s.id);
        if (s.quantity < 0 && s.quantity != kUnlimited)
            addError(out, "cutting.job.stock_quantity_invalid",
                     name + " has quantity " + std::to_string(s.quantity) +
                         "; use a value >= 0 or kUnlimited (-1).",
                     s.id);
        if (!std::isfinite(s.cost))
            addError(out, "cutting.job.stock_cost_invalid", name + " has a non-finite cost.", s.id);
        if (!positive(s.length) || !positive(s.width)) {
            addError(out, "cutting.job.stock_size_invalid",
                     name + " has an invalid size " + std::to_string(s.length) + " x " + std::to_string(s.width) +
                         " mm; both sides must be > 0.",
                     s.id);
            continue;
        }
        if (!nonNegative(job.trim.left) || !nonNegative(job.trim.right) || !nonNegative(job.trim.top) ||
            !nonNegative(job.trim.bottom))
            continue;
        const Rect u = usableArea(s, job.trim);
        if (!positive(u.length) || !positive(u.width))
            addError(out, "cutting.job.stock_no_usable_area",
                     name + " " + formatLength(s.length) + " x " + formatLength(s.width) +
                         " mm has no usable area after the edge trim (left " + formatLength(job.trim.left) +
                         ", right " + formatLength(job.trim.right) + ", bottom " + formatLength(job.trim.bottom) +
                         ", top " + formatLength(job.trim.top) + " mm).",
                     s.id);
    }
    return out;
}

int areaLowerBoundOnSheetCount(const Job& job, double pieceArea) {
    const double tol = 1e-3; // mm^2
    if (!(pieceArea > tol)) return 0;
    // Usable areas, largest first. An unlimited type larger than everything after it ends the search.
    struct Type {
        double area;
        int quantity;
    };
    std::vector<Type> types;
    for (const StockSheet& s : job.stock) {
        const Rect u = usableArea(s, job.trim);
        if (!(u.length > core::kLengthEps) || !(u.width > core::kLengthEps)) continue;
        if (s.quantity == 0) continue;
        types.push_back({u.area(), s.quantity});
    }
    std::stable_sort(types.begin(), types.end(), [](const Type& a, const Type& b) { return a.area > b.area; });
    double remaining = pieceArea;
    long long count = 0;
    for (const Type& t : types) {
        const double needed = std::ceil((remaining - tol) / t.area);
        if (t.quantity == kUnlimited || needed <= static_cast<double>(t.quantity)) {
            count += static_cast<long long>(needed);
            return static_cast<int>(std::min<long long>(count, 2000000000LL));
        }
        count += t.quantity;
        remaining -= t.area * static_cast<double>(t.quantity);
    }
    return -1;
}

} // namespace fos::cutting
