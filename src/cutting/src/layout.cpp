// SPDX-License-Identifier: AGPL-3.0-only
#include "layout.hpp"

#include "fos/core/units.hpp"
#include "fos/cutting/optimizer.hpp"

#include <algorithm>

namespace fos::cutting::detail {
namespace {

constexpr double kEps = core::kLengthEps;

/// Trim cuts in execution order: left, right, bottom, top (only where the trim is > 0).
std::vector<Cut> trimCuts(const Job& job, const StockSheet& stock) {
    std::vector<Cut> cuts;
    Rect material{0, 0, stock.length, stock.width};
    const auto add = [&](CutOrientation o, double position, const Rect& after) {
        Cut c;
        c.kind = CutKind::Trim;
        c.orientation = o;
        c.position = position;
        c.kerfWidth = 0;
        c.extent = o == CutOrientation::Crosswise ? material.width : material.length;
        c.source = material;
        c.stage = 0;
        c.node = -1;
        cuts.push_back(c);
        material = after;
    };
    const Trim& t = job.trim;
    if (t.left > kEps)
        add(CutOrientation::Crosswise, t.left, Rect{t.left, material.y, material.length - t.left, material.width});
    if (t.right > kEps)
        add(CutOrientation::Crosswise, stock.length - t.right,
            Rect{material.x, material.y, material.length - t.right, material.width});
    if (t.bottom > kEps)
        add(CutOrientation::Lengthwise, t.bottom,
            Rect{material.x, t.bottom, material.length, material.width - t.bottom});
    if (t.top > kEps)
        add(CutOrientation::Lengthwise, stock.width - t.top,
            Rect{material.x, material.y, material.length, material.width - t.top});
    return cuts;
}

int trimCutCount(const Job& job) {
    const Trim& t = job.trim;
    return (t.left > kEps ? 1 : 0) + (t.right > kEps ? 1 : 0) + (t.bottom > kEps ? 1 : 0) + (t.top > kEps ? 1 : 0);
}

double trimCutLength(const Job& job, const StockSheet& s) {
    const Trim& t = job.trim;
    double len = 0;
    double length = s.length;
    if (t.left > kEps) { len += s.width; length -= t.left; }
    if (t.right > kEps) { len += s.width; length -= t.right; }
    if (t.bottom > kEps) len += length;
    if (t.top > kEps) len += length;
    return len;
}

double cutExtent(const CutNode& n) {
    return n.orientation == CutOrientation::Crosswise ? n.rect.width : n.rect.length;
}

} // namespace

RunStats computeStats(const Job& job, const std::vector<Instance>& instances, const PackResult& run) {
    RunStats st;
    for (int idx : run.unplaced) {
        const Instance& in = instances[static_cast<std::size_t>(idx)];
        st.unplacedArea += in.length * in.width;
        ++st.unplacedCount;
    }
    st.sheets = static_cast<int>(run.sheets.size());
    for (const PackedSheet& sheet : run.sheets) {
        const StockSheet& stock = job.stock[static_cast<std::size_t>(sheet.stock)];
        const double area = stock.length * stock.width;
        st.sheetArea += area;
        st.cost += stock.cost;
        if (!stock.isRemnant) {
            ++st.fullSheets;
            st.fullSheetArea += area;
        }
        st.cuts += trimCutCount(job);
        st.cutLength += trimCutLength(job, stock);
        for (const Placed& p : sheet.placed) {
            st.usedArea += p.length * p.width;
            if (stock.isRemnant) st.usedOnRemnants += p.length * p.width;
        }
        for (const CutNode& n : sheet.nodes) {
            if (n.kind == NodeKind::Cut) {
                ++st.cuts;
                st.cutLength += cutExtent(n);
            } else if (n.kind == NodeKind::Offcut && isReusableRemnant(job, n.rect.length, n.rect.width)) {
                st.remnantArea += n.rect.area();
                st.largestRemnant = std::max(st.largestRemnant, n.rect.area());
            }
        }
    }
    return st;
}

SheetLayout buildSheetLayout(const Job& job, const std::vector<Instance>& instances, const PackedSheet& sheet) {
    const StockSheet& stock = job.stock[static_cast<std::size_t>(sheet.stock)];
    SheetLayout out;
    out.stockId = stock.id;
    out.stockIndex = sheet.stock;
    out.sheetLength = stock.length;
    out.sheetWidth = stock.width;
    out.usable = sheet.usable;
    out.sheetArea = stock.length * stock.width;
    out.usableArea = sheet.usable.area();
    out.trimArea = out.sheetArea - out.usableArea;

    for (const Placed& p : sheet.placed) {
        const Instance& in = instances[static_cast<std::size_t>(p.inst)];
        Placement pl;
        pl.pieceId = job.pieces[static_cast<std::size_t>(in.piece)].id;
        pl.instance = in.instance;
        pl.x = p.x;
        pl.y = p.y;
        pl.length = p.length;
        pl.width = p.width;
        pl.rotated = p.rotated;
        out.usedArea += p.length * p.width;
        out.placements.push_back(std::move(pl));
    }

    out.tree = sheet.nodes;
    out.cuts = trimCuts(job, stock);
    // Pre-order walk: a cut, then everything on its low side, then its high side.
    std::vector<int> stack{0};
    while (!stack.empty()) {
        const int idx = stack.back();
        stack.pop_back();
        CutNode& n = out.tree[static_cast<std::size_t>(idx)];
        if (n.kind == NodeKind::Cut) {
            Cut c;
            c.kind = CutKind::Split;
            c.orientation = n.orientation;
            c.position = n.position;
            c.kerfWidth = n.kerfWidth;
            c.extent = cutExtent(n);
            c.source = n.rect;
            c.stage = n.stage;
            c.node = idx;
            out.kerfArea += c.kerfWidth * c.extent;
            out.stageCount = std::max(out.stageCount, n.stage);
            out.cuts.push_back(c);
            if (n.high >= 0) stack.push_back(n.high);
            stack.push_back(n.low);
        } else if (n.kind == NodeKind::Offcut) {
            Offcut o;
            o.rect = n.rect;
            o.reusable = isReusableRemnant(job, n.rect.length, n.rect.width);
            (o.reusable ? out.remnantArea : out.wasteArea) += n.rect.area();
            n.item = static_cast<int>(out.offcuts.size());
            out.offcuts.push_back(o);
        }
    }
    for (const Cut& c : out.cuts) out.cutLength += c.extent;
    out.utilisationOfSheetPct = out.sheetArea > 0 ? 100.0 * out.usedArea / out.sheetArea : 0.0;
    out.utilisationOfUsablePct = out.usableArea > 0 ? 100.0 * out.usedArea / out.usableArea : 0.0;
    return out;
}

void fillTotals(const Job& job, CuttingResult& result) {
    Totals t;
    result.stockUsage.clear();
    for (const StockSheet& s : job.stock) result.stockUsage.push_back({s.id, 0});
    for (const SheetLayout& s : result.sheets) {
        ++t.sheetCount;
        t.placedCount += static_cast<int>(s.placements.size());
        t.sheetArea += s.sheetArea;
        t.usableArea += s.usableArea;
        t.trimArea += s.trimArea;
        t.usedArea += s.usedArea;
        t.kerfArea += s.kerfArea;
        t.remnantArea += s.remnantArea;
        t.wasteArea += s.wasteArea;
        t.cutCount += static_cast<int>(s.cuts.size());
        t.cutLength += s.cutLength;
        t.stockCost += job.stock[static_cast<std::size_t>(s.stockIndex)].cost;
        ++result.stockUsage[static_cast<std::size_t>(s.stockIndex)].sheetsUsed;
    }
    for (const UnplacedPiece& u : result.unplaced) t.unplacedCount += u.quantity;
    t.utilisationOfSheetPct = t.sheetArea > 0 ? 100.0 * t.usedArea / t.sheetArea : 0.0;
    t.utilisationOfUsablePct = t.usableArea > 0 ? 100.0 * t.usedArea / t.usableArea : 0.0;
    result.totals = t;
}

} // namespace fos::cutting::detail
