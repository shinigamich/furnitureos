// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/machining/report.hpp"

#include "fos/machining/face.hpp"

#include <algorithm>
#include <tuple>

namespace fos::machining {

std::vector<DrillingRow> drillingRows(const domain::Part& part) {
    std::vector<DrillingRow> rows;
    for (const auto& op : part.operations) {
        const auto* h = std::get_if<domain::Hole>(&op.shape);
        if (!h) continue;
        DrillingRow r;
        r.partId = part.id;
        r.partName = part.name;
        r.operationId = op.id;
        r.face = h->face;
        r.u = h->u;
        r.v = h->v;
        r.diameter = h->diameter;
        r.depth = effectiveDepth(part, *h);
        r.through = h->through;
        r.source = op.source;
        rows.push_back(std::move(r));
    }
    std::stable_sort(rows.begin(), rows.end(), [](const DrillingRow& a, const DrillingRow& b) {
        return std::tie(a.face, a.u, a.v, a.diameter, a.depth, a.operationId) <
               std::tie(b.face, b.u, b.v, b.diameter, b.depth, b.operationId);
    });
    return rows;
}

std::vector<DrillingRow> drillingRows(const domain::Model& model) {
    std::vector<DrillingRow> rows;
    for (const auto& assembly : model.assemblies)
        for (const auto& part : assembly.parts)
            for (auto& r : drillingRows(part)) {
                r.assemblyId = assembly.id;
                rows.push_back(std::move(r));
            }
    return rows;
}

std::vector<DrillSummaryRow> drillSummary(const domain::Model& model) {
    std::vector<DrillingRow> rows = drillingRows(model);
    std::stable_sort(rows.begin(), rows.end(),
                     [](const DrillingRow& a, const DrillingRow& b) { return a.diameter < b.diameter; });
    std::vector<DrillSummaryRow> out;
    double previous = 0;
    for (const auto& r : rows) {
        if (out.empty() || r.diameter - previous > core::kLengthEps) out.push_back({r.diameter, 0, 0, 0.0});
        previous = r.diameter;
        ++out.back().holeCount;
        if (r.through) ++out.back().throughCount;
        else out.back().maxBlindDepth = std::max(out.back().maxBlindDepth, r.depth);
    }
    return out;
}

} // namespace fos::machining
