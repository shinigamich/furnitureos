// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/machining/ordering.hpp"

#include <algorithm>
#include <limits>

namespace fos::machining {
namespace {
using geometry::Vec2;

struct Item {
    std::size_t index;
    domain::Face face;
    ToolKind tool;
    double size;
    Vec2 entry, exit;
};

Item describe(std::size_t index, const domain::Operation& op) {
    Item it{index, domain::faceOf(op), ToolKind::Drill, 0, {}, {}};
    if (const auto* h = std::get_if<domain::Hole>(&op.shape)) {
        it.size = h->diameter;
        it.entry = it.exit = {h->u, h->v};
    } else if (const auto* g = std::get_if<domain::Groove>(&op.shape)) {
        it.tool = ToolKind::GrooveCutter;
        it.size = g->width;
        it.entry = {g->u1, g->v1};
        it.exit = {g->u2, g->v2};
    } else if (const auto* p = std::get_if<domain::Pocket>(&op.shape)) {
        it.tool = ToolKind::PocketMill;
        if (!p->contour.empty()) it.entry = it.exit = p->contour.front();
    }
    return it;
}
} // namespace

const char* toString(ToolKind k) noexcept {
    switch (k) {
    case ToolKind::Drill: return "drill";
    case ToolKind::GrooveCutter: return "groove cutter";
    case ToolKind::PocketMill: return "pocket mill";
    }
    return "?";
}

OperationOrder orderOperations(const domain::Part& part) {
    std::vector<Item> items;
    for (std::size_t i = 0; i < part.operations.size(); ++i) items.push_back(describe(i, part.operations[i]));
    std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
        if (a.face != b.face) return a.face < b.face;
        if (a.tool != b.tool) return a.tool < b.tool;
        return a.size < b.size;
    });

    OperationOrder result;
    std::size_t begin = 0;
    while (begin < items.size()) {
        std::size_t end = begin + 1;
        while (end < items.size() && items[end].face == items[begin].face && items[end].tool == items[begin].tool &&
               items[end].size - items[end - 1].size <= core::kLengthEps)
            ++end;

        OperationGroup g;
        g.face = items[begin].face;
        g.tool = items[begin].tool;
        g.toolSize = items[begin].size;
        std::vector<Item> pending(items.begin() + static_cast<std::ptrdiff_t>(begin),
                                  items.begin() + static_cast<std::ptrdiff_t>(end));
        std::sort(pending.begin(), pending.end(), [](const Item& a, const Item& b) { return a.index < b.index; });
        Vec2 pos{0, 0};
        while (!pending.empty()) {
            std::size_t best = 0;
            double bestD = std::numeric_limits<double>::infinity();
            for (std::size_t k = 0; k < pending.size(); ++k) {
                const double d = length(pending[k].entry - pos);
                if (d < bestD - core::kLengthEps) { // strict: ties keep the lower index
                    bestD = d;
                    best = k;
                }
            }
            g.order.push_back(pending[best].index);
            g.rapidTravel += bestD;
            pos = pending[best].exit;
            pending.erase(pending.begin() + static_cast<std::ptrdiff_t>(best));
        }
        result.totalRapidTravel += g.rapidTravel;
        result.groups.push_back(std::move(g));
        begin = end;
    }
    return result;
}

} // namespace fos::machining
