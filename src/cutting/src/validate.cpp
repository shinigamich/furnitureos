// SPDX-License-Identifier: AGPL-3.0-only
// Independent layout validator. Deliberately shares no code with the packer: the usable
// area, kerf gaps, the guillotine replay, remnant classification and all areas are
// recomputed here from the public job and result data only.
#include "fos/cutting/validate.hpp"

#include "fos/core/units.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace fos::cutting {
namespace {

using core::formatLength;
constexpr double kEps = core::kLengthEps;

struct Checker {
    const Job& job;
    core::Diagnostics out;

    void error(const char* code, std::string message, const std::string& entity = {}) {
        out.push_back({core::Severity::Error, code, std::move(message), entity});
    }
};

std::string rectText(const Rect& r) {
    return formatLength(r.length, 3) + " x " + formatLength(r.width, 3) + " at (" + formatLength(r.x, 3) + ", " +
           formatLength(r.y, 3) + ")";
}

bool sameRect(const Rect& a, const Rect& b) {
    return std::fabs(a.x - b.x) <= kEps && std::fabs(a.y - b.y) <= kEps && std::fabs(a.length - b.length) <= kEps &&
           std::fabs(a.width - b.width) <= kEps;
}

bool inside(const Rect& inner, const Rect& outer) {
    return inner.x >= outer.x - kEps && inner.y >= outer.y - kEps &&
           inner.x + inner.length <= outer.x + outer.length + kEps &&
           inner.y + inner.width <= outer.y + outer.width + kEps;
}

double areaTol(double sheetArea) { return std::max(1e-3, 1e-9 * sheetArea); }

/// A piece of material during the replay, with the cut that produced it.
struct Material {
    Rect rect;
    bool hasOrientation = false;
    CutOrientation orientation = CutOrientation::Crosswise;
    int stage = 0;
};

/// Replays the cut sequence on the full sheet. Returns the pieces of material left over.
std::vector<Material> replay(Checker& c, const SheetLayout& sheet, const Rect& usable, const std::string& where,
                             double& kerfArea, double& cutLength, int& maxStage) {
    std::vector<Material> material{{Rect{0, 0, sheet.sheetLength, sheet.sheetWidth}, false, {}, 0}};
    for (std::size_t ci = 0; ci < sheet.cuts.size(); ++ci) {
        const Cut& cut = sheet.cuts[ci];
        const std::string cutName = "Cut #" + std::to_string(ci + 1) + " on " + where;
        const auto it = std::find_if(material.begin(), material.end(),
                                     [&](const Material& m) { return sameRect(m.rect, cut.source); });
        if (it == material.end()) {
            c.error("cutting.layout.cut_source_missing",
                    cutName + " cuts a piece of material " + rectText(cut.source) +
                        " that does not exist at that point of the sequence.");
            continue;
        }
        const Material src = *it;
        material.erase(it);
        const bool cross = cut.orientation == CutOrientation::Crosswise;
        const double start = cross ? src.rect.x : src.rect.y;
        const double end = start + (cross ? src.rect.length : src.rect.width);
        const double across = cross ? src.rect.width : src.rect.length;
        if (std::fabs(cut.extent - across) > kEps)
            c.error("cutting.layout.cut_not_edge_to_edge",
                    cutName + " has extent " + formatLength(cut.extent, 3) + " mm but the material it cuts is " +
                        formatLength(across, 3) + " mm across; a guillotine cut must run edge to edge.");
        if (!(cut.position > start + kEps) || !(cut.position < end - kEps)) {
            c.error("cutting.layout.cut_outside_material",
                    cutName + " at " + formatLength(cut.position, 3) + " mm lies outside the material it cuts (" +
                        formatLength(start, 3) + " .. " + formatLength(end, 3) + " mm).");
            material.push_back(src);
            continue;
        }
        cutLength += across;
        const auto part = [&](double from, double to) {
            Material m = src;
            if (cross) {
                m.rect.x = from;
                m.rect.length = to - from;
            } else {
                m.rect.y = from;
                m.rect.width = to - from;
            }
            return m;
        };
        if (cut.kind == CutKind::Trim) {
            if (std::fabs(cut.kerfWidth) > kEps)
                c.error("cutting.layout.trim_cut_kerf", cutName + " is a trim cut but reports a kerf width.");
            const double uStart = cross ? usable.x : usable.y;
            const double uEnd = uStart + (cross ? usable.length : usable.width);
            if (cut.position <= uStart + kEps) {
                material.push_back(part(cut.position, end)); // low strip is trim scrap
            } else if (cut.position >= uEnd - kEps) {
                material.push_back(part(start, cut.position)); // high strip is trim scrap
            } else {
                c.error("cutting.layout.trim_cut_in_usable_area",
                        cutName + " is marked as a trim cut but runs through the usable area at " +
                            formatLength(cut.position, 3) + " mm.");
                material.push_back(src);
            }
            continue;
        }
        if (!inside(src.rect, usable))
            c.error("cutting.layout.split_before_trim",
                    cutName + " splits material " + rectText(src.rect) + " that still contains edge trim.");
        const double expectedKerf = std::min(c.job.kerf, end - cut.position);
        if (std::fabs(cut.kerfWidth - expectedKerf) > kEps)
            c.error("cutting.layout.kerf_width",
                    cutName + " reports a kerf width of " + formatLength(cut.kerfWidth, 3) + " mm, expected " +
                        formatLength(expectedKerf, 3) + " mm.");
        kerfArea += expectedKerf * across;
        const int stage = src.hasOrientation && src.orientation == cut.orientation ? src.stage : src.stage + 1;
        if (cut.stage != stage)
            c.error("cutting.layout.cut_stage",
                    cutName + " reports stage " + std::to_string(cut.stage) + " but is a stage " +
                        std::to_string(stage) + " cut.");
        maxStage = std::max(maxStage, stage);
        Material low = part(start, cut.position);
        low.hasOrientation = true;
        low.orientation = cut.orientation;
        low.stage = stage;
        material.push_back(low);
        if (end - (cut.position + expectedKerf) > kEps) {
            Material high = part(cut.position + expectedKerf, end);
            high.hasOrientation = true;
            high.orientation = cut.orientation;
            high.stage = stage;
            material.push_back(high);
        }
    }
    return material;
}

} // namespace

namespace {

void checkSheet(Checker& c, const SheetLayout& sheet, std::size_t si, std::map<std::string, std::set<int>>& placedInstances,
                const std::map<std::string, const Piece*>& pieces) {
    const Job& job = c.job;
    const std::string where = "sheet " + std::to_string(si + 1) + " ('" + sheet.stockId + "')";
    if (sheet.stockIndex < 0 || sheet.stockIndex >= static_cast<int>(job.stock.size()) ||
        job.stock[static_cast<std::size_t>(sheet.stockIndex)].id != sheet.stockId) {
        c.error("cutting.layout.unknown_stock",
                "Layout " + where + " does not refer to a stock sheet of job '" + job.id + "'.", sheet.stockId);
        return;
    }
    const StockSheet& stock = job.stock[static_cast<std::size_t>(sheet.stockIndex)];
    if (std::fabs(stock.length - sheet.sheetLength) > kEps || std::fabs(stock.width - sheet.sheetWidth) > kEps)
        c.error("cutting.layout.stock_size",
                "Layout " + where + " reports a sheet size of " + formatLength(sheet.sheetLength) + " x " +
                    formatLength(sheet.sheetWidth) + " mm but the stock sheet is " + formatLength(stock.length) +
                    " x " + formatLength(stock.width) + " mm.",
                sheet.stockId);
    const Rect usable{job.trim.left, job.trim.bottom, stock.length - job.trim.left - job.trim.right,
                      stock.width - job.trim.bottom - job.trim.top};
    if (!sameRect(usable, sheet.usable))
        c.error("cutting.layout.usable_area",
                "Layout " + where + " reports the usable area " + rectText(sheet.usable) + ", expected " +
                    rectText(usable) + ".");

    // Placements: identity, size, rotation, position.
    double usedArea = 0;
    std::vector<Rect> rects;
    for (std::size_t i = 0; i < sheet.placements.size(); ++i) {
        const Placement& pl = sheet.placements[i];
        const Rect r{pl.x, pl.y, pl.length, pl.width};
        rects.push_back(r);
        usedArea += r.area();
        const std::string name = "Placement #" + std::to_string(i + 1) + " ('" + pl.pieceId + "', instance " +
                                 std::to_string(pl.instance) + ") on " + where;
        const auto it = pieces.find(pl.pieceId);
        if (it == pieces.end()) {
            c.error("cutting.layout.unknown_piece", name + " refers to a piece that is not in the job.", pl.pieceId);
            continue;
        }
        const Piece& p = *it->second;
        if (pl.instance < 0 || pl.instance >= p.quantity)
            c.error("cutting.layout.instance_out_of_range",
                    name + " has an instance index outside 0.." + std::to_string(p.quantity - 1) + ".", pl.pieceId);
        else if (!placedInstances[pl.pieceId].insert(pl.instance).second)
            c.error("cutting.layout.duplicate_instance", name + " is placed more than once.", pl.pieceId);
        if (pl.rotated && !p.canRotate)
            c.error("cutting.layout.illegal_rotation",
                    name + " is rotated but the piece must keep its length along the sheet length (grain).",
                    pl.pieceId);
        const double expL = pl.rotated ? p.width : p.length;
        const double expW = pl.rotated ? p.length : p.width;
        if (std::fabs(pl.length - expL) > kEps || std::fabs(pl.width - expW) > kEps)
            c.error("cutting.layout.wrong_size",
                    name + " is " + formatLength(pl.length, 3) + " x " + formatLength(pl.width, 3) +
                        " mm but should be " + formatLength(expL, 3) + " x " + formatLength(expW, 3) + " mm.",
                    pl.pieceId);
        if (!inside(r, usable))
            c.error("cutting.layout.outside_usable_area",
                    name + " (" + rectText(r) + ") is not inside the usable area " + rectText(usable) + ".",
                    pl.pieceId);
    }
    // Pairwise separation by at least one kerf along X or Y.
    for (std::size_t a = 0; a < rects.size(); ++a)
        for (std::size_t b = a + 1; b < rects.size(); ++b) {
            const double gapX = std::max(rects[a].x - (rects[b].x + rects[b].length),
                                         rects[b].x - (rects[a].x + rects[a].length));
            const double gapY = std::max(rects[a].y - (rects[b].y + rects[b].width),
                                         rects[b].y - (rects[a].y + rects[a].width));
            if (gapX < job.kerf - kEps && gapY < job.kerf - kEps)
                c.error("cutting.layout.overlap_or_kerf",
                        "Placements #" + std::to_string(a + 1) + " and #" + std::to_string(b + 1) + " on " + where +
                            " are closer than the saw kerf of " + formatLength(job.kerf, 3) + " mm (gap " +
                            formatLength(std::max(gapX, gapY), 3) + " mm).");
        }

    // Replay of the cut sequence must leave exactly the placements and the offcuts.
    double kerfArea = 0, cutLength = 0;
    int maxStage = 0;
    const std::vector<Material> left = replay(c, sheet, usable, where, kerfArea, cutLength, maxStage);
    std::vector<char> taken(left.size(), 0);
    const auto claim = [&](const Rect& r) {
        for (std::size_t i = 0; i < left.size(); ++i)
            if (!taken[i] && sameRect(left[i].rect, r)) {
                taken[i] = 1;
                return true;
            }
        return false;
    };
    for (std::size_t i = 0; i < rects.size(); ++i)
        if (!claim(rects[i]))
            c.error("cutting.layout.placement_not_cut",
                    "Placement #" + std::to_string(i + 1) + " (" + rectText(rects[i]) + ") on " + where +
                        " is not produced by the cut sequence.");
    double remnantArea = 0, wasteArea = 0;
    for (std::size_t i = 0; i < sheet.offcuts.size(); ++i) {
        const Offcut& o = sheet.offcuts[i];
        if (!claim(o.rect))
            c.error("cutting.layout.offcut_not_cut",
                    "Offcut #" + std::to_string(i + 1) + " (" + rectText(o.rect) + ") on " + where +
                        " is not produced by the cut sequence.");
        const double longer = std::max(o.rect.length, o.rect.width), shorter = std::min(o.rect.length, o.rect.width);
        const bool reusable = longer >= job.minRemnantLength - kEps && shorter >= job.minRemnantWidth - kEps;
        if (reusable != o.reusable)
            c.error("cutting.layout.offcut_classification",
                    "Offcut #" + std::to_string(i + 1) + " (" + rectText(o.rect) + ") on " + where + " is marked " +
                        (o.reusable ? "reusable" : "waste") + " but the minimum remnant size is " +
                        formatLength(job.minRemnantLength) + " x " + formatLength(job.minRemnantWidth) + " mm.");
        (reusable ? remnantArea : wasteArea) += o.rect.area();
    }
    for (std::size_t i = 0; i < left.size(); ++i)
        if (!taken[i])
            c.error("cutting.layout.unaccounted_material",
                    "The cut sequence of " + where + " leaves material " + rectText(left[i].rect) +
                        " that is neither a placed piece nor a listed offcut.");
    if (job.maxStages != 0 && maxStage > job.maxStages)
        c.error("cutting.layout.too_many_stages",
                "The cut sequence of " + where + " needs " + std::to_string(maxStage) +
                    " cutting stages but the job allows " + std::to_string(job.maxStages) + ".");
    if (sheet.stageCount != maxStage)
        c.error("cutting.layout.stage_count",
                "Layout " + where + " reports " + std::to_string(sheet.stageCount) + " stages, the cut sequence has " +
                    std::to_string(maxStage) + ".");

    // Cut tree must describe the same cuts and leaves as the flat data.
    std::size_t treeCuts = 0, splitCuts = 0;
    for (const CutNode& n : sheet.tree)
        if (n.kind == NodeKind::Cut) ++treeCuts;
    bool treeOk = !sheet.tree.empty() && sameRect(sheet.tree[0].rect, usable);
    for (const Cut& cut : sheet.cuts) {
        if (cut.kind != CutKind::Split) continue;
        ++splitCuts;
        if (cut.node < 0 || cut.node >= static_cast<int>(sheet.tree.size())) {
            treeOk = false;
            continue;
        }
        const CutNode& n = sheet.tree[static_cast<std::size_t>(cut.node)];
        if (n.kind != NodeKind::Cut || n.orientation != cut.orientation || std::fabs(n.position - cut.position) > kEps ||
            !sameRect(n.rect, cut.source))
            treeOk = false;
    }
    for (const CutNode& n : sheet.tree) {
        if (n.kind == NodeKind::Piece &&
            (n.item < 0 || n.item >= static_cast<int>(rects.size()) || !sameRect(n.rect, rects[static_cast<std::size_t>(n.item)])))
            treeOk = false;
        if (n.kind == NodeKind::Offcut &&
            (n.item < 0 || n.item >= static_cast<int>(sheet.offcuts.size()) ||
             !sameRect(n.rect, sheet.offcuts[static_cast<std::size_t>(n.item)].rect)))
            treeOk = false;
    }
    if (!treeOk || treeCuts != splitCuts)
        c.error("cutting.layout.tree_mismatch",
                "The cut tree of " + where + " does not match its cut sequence, placements and offcuts.");

    // Areas.
    const double sheetArea = stock.length * stock.width;
    const double usableAreaValue = usable.area();
    const double tol = areaTol(sheetArea);
    const auto checkArea = [&](const char* what, double reported, double expected) {
        if (std::fabs(reported - expected) > tol)
            c.error("cutting.layout.area_mismatch",
                    std::string("Layout ") + where + " reports " + what + " = " + formatLength(reported, 3) +
                        " mm2, recomputed " + formatLength(expected, 3) + " mm2.");
    };
    checkArea("sheetArea", sheet.sheetArea, sheetArea);
    checkArea("usableArea", sheet.usableArea, usableAreaValue);
    checkArea("trimArea", sheet.trimArea, sheetArea - usableAreaValue);
    checkArea("usedArea", sheet.usedArea, usedArea);
    checkArea("kerfArea", sheet.kerfArea, kerfArea);
    checkArea("remnantArea", sheet.remnantArea, remnantArea);
    checkArea("wasteArea", sheet.wasteArea, wasteArea);
    checkArea("cutLength", sheet.cutLength, cutLength);
    if (std::fabs(usableAreaValue - (usedArea + kerfArea + remnantArea + wasteArea)) > tol)
        c.error("cutting.layout.area_balance",
                "On " + where + " pieces, kerf, remnants and waste add up to " +
                    formatLength(usedArea + kerfArea + remnantArea + wasteArea, 3) + " mm2 but the usable area is " +
                    formatLength(usableAreaValue, 3) + " mm2.");
    if (std::fabs(sheet.utilisationOfSheetPct - 100.0 * usedArea / sheetArea) > 1e-6 ||
        std::fabs(sheet.utilisationOfUsablePct - 100.0 * usedArea / usableAreaValue) > 1e-6)
        c.error("cutting.layout.utilisation",
                "Layout " + where + " reports utilisation " + formatLength(sheet.utilisationOfSheetPct, 3) + " % / " +
                    formatLength(sheet.utilisationOfUsablePct, 3) + " % that does not match its areas.");
}

} // namespace

core::Diagnostics validateLayout(const Job& job, const CuttingResult& result) {
    Checker c{job, {}};
    std::map<std::string, const Piece*> pieces;
    for (const Piece& p : job.pieces) pieces.emplace(p.id, &p);
    std::map<std::string, std::set<int>> placedInstances;

    for (std::size_t si = 0; si < result.sheets.size(); ++si)
        checkSheet(c, result.sheets[si], si, placedInstances, pieces);

    // Quantities: placed + unplaced == required.
    std::map<std::string, int> unplaced;
    for (const UnplacedPiece& u : result.unplaced) {
        if (pieces.find(u.pieceId) == pieces.end()) {
            c.error("cutting.layout.unknown_piece",
                    "The unplaced list names piece '" + u.pieceId + "' that is not in the job.", u.pieceId);
            continue;
        }
        if (u.quantity < 1 || u.message.empty())
            c.error("cutting.layout.unplaced_entry",
                    "The unplaced entry for piece '" + u.pieceId + "' has no quantity or no reason.", u.pieceId);
        unplaced[u.pieceId] += u.quantity;
    }
    int placedTotal = 0, unplacedTotal = 0;
    for (const Piece& p : job.pieces) {
        const int placed = static_cast<int>(placedInstances[p.id].size());
        const int missing = unplaced[p.id];
        placedTotal += placed;
        unplacedTotal += missing;
        if (placed + missing != p.quantity)
            c.error("cutting.layout.quantity_mismatch",
                    "Piece '" + p.id + "' is required " + std::to_string(p.quantity) + " times but " +
                        std::to_string(placed) + " are placed and " + std::to_string(missing) +
                        " are reported unplaced.",
                    p.id);
    }

    // Stock usage and limits.
    std::vector<int> used(job.stock.size(), 0);
    Totals t;
    for (const SheetLayout& s : result.sheets) {
        if (s.stockIndex >= 0 && s.stockIndex < static_cast<int>(job.stock.size())) {
            ++used[static_cast<std::size_t>(s.stockIndex)];
            t.stockCost += job.stock[static_cast<std::size_t>(s.stockIndex)].cost;
        }
        t.sheetArea += s.sheetArea;
        t.usableArea += s.usableArea;
        t.trimArea += s.trimArea;
        t.usedArea += s.usedArea;
        t.kerfArea += s.kerfArea;
        t.remnantArea += s.remnantArea;
        t.wasteArea += s.wasteArea;
        t.cutLength += s.cutLength;
        t.cutCount += static_cast<int>(s.cuts.size());
    }
    for (std::size_t i = 0; i < job.stock.size(); ++i) {
        const StockSheet& s = job.stock[i];
        if (s.quantity != kUnlimited && used[i] > s.quantity)
            c.error("cutting.layout.stock_exceeded",
                    "The layout uses " + std::to_string(used[i]) + " sheets of stock '" + s.id + "' but only " +
                        std::to_string(s.quantity) + " are available.",
                    s.id);
        if (i >= result.stockUsage.size() || result.stockUsage[i].stockId != s.id ||
            result.stockUsage[i].sheetsUsed != used[i])
            c.error("cutting.layout.stock_usage",
                    "The reported sheet count for stock '" + s.id + "' does not match the layout (" +
                        std::to_string(used[i]) + " sheets).",
                    s.id);
    }
    if (result.stockUsage.size() != job.stock.size())
        c.error("cutting.layout.stock_usage", "The stock usage list does not have one entry per stock sheet type.");

    // Totals.
    const Totals& r = result.totals;
    const double tol = areaTol(t.sheetArea) * static_cast<double>(std::max<std::size_t>(1, result.sheets.size()));
    const auto differs = [&](double a, double b) { return std::fabs(a - b) > tol; };
    const double utilSheet = t.sheetArea > 0 ? 100.0 * t.usedArea / t.sheetArea : 0.0;
    const double utilUsable = t.usableArea > 0 ? 100.0 * t.usedArea / t.usableArea : 0.0;
    if (r.sheetCount != static_cast<int>(result.sheets.size()) || r.placedCount != placedTotal ||
        r.unplacedCount != unplacedTotal || r.cutCount != t.cutCount || differs(r.sheetArea, t.sheetArea) ||
        differs(r.usableArea, t.usableArea) || differs(r.trimArea, t.trimArea) || differs(r.usedArea, t.usedArea) ||
        differs(r.kerfArea, t.kerfArea) || differs(r.remnantArea, t.remnantArea) ||
        differs(r.wasteArea, t.wasteArea) || differs(r.cutLength, t.cutLength) ||
        std::fabs(r.stockCost - t.stockCost) > 1e-6 * std::max(1.0, std::fabs(t.stockCost)) ||
        std::fabs(r.utilisationOfSheetPct - utilSheet) > 1e-6 ||
        std::fabs(r.utilisationOfUsablePct - utilUsable) > 1e-6)
        c.error("cutting.layout.totals_mismatch",
                "The totals of job '" + job.id + "' do not match the sums over its " +
                    std::to_string(result.sheets.size()) + " sheet layouts.",
                job.id);

    // Lower bound on the sheet count, recomputed sheet by sheet: always take the largest
    // usable area still available until the placed piece area is covered.
    {
        std::vector<std::pair<double, int>> types; // usable area, quantity
        for (const StockSheet& s : job.stock)
            types.emplace_back((s.length - job.trim.left - job.trim.right) * (s.width - job.trim.bottom - job.trim.top),
                               s.quantity);
        std::stable_sort(types.begin(), types.end(),
                         [](const auto& a, const auto& b) { return a.first > b.first; });
        double remaining = t.usedArea;
        int bound = 0;
        std::size_t ti = 0;
        while (remaining > 1e-3 && ti < types.size() && bound <= static_cast<int>(result.sheets.size())) {
            if (types[ti].second == 0 || !(types[ti].first > 0)) {
                ++ti;
                continue;
            }
            remaining -= types[ti].first;
            ++bound;
            if (types[ti].second != kUnlimited) --types[ti].second;
        }
        if (bound != result.sheetCountLowerBound)
            c.error("cutting.layout.lower_bound",
                    "The reported lower bound on the sheet count is " + std::to_string(result.sheetCountLowerBound) +
                        ", the area-based bound is " + std::to_string(bound) + ".",
                    job.id);
        const bool minimal = result.unplaced.empty() && static_cast<int>(result.sheets.size()) == bound;
        if (result.isSheetCountProvenMinimal != minimal)
            c.error("cutting.layout.minimal_flag",
                    "isSheetCountProvenMinimal is " + std::string(result.isSheetCountProvenMinimal ? "true" : "false") +
                        " but " + std::to_string(result.sheets.size()) + " sheets are used against a lower bound of " +
                        std::to_string(bound) + ".",
                    job.id);
        if (result.isProvenOptimal)
            c.error("cutting.layout.optimality_claim",
                    "The result claims to be proven optimal; the heuristic optimiser cannot prove that.", job.id);
    }
    return c.out;
}

} // namespace fos::cutting
