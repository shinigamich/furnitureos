// SPDX-License-Identifier: AGPL-3.0-only
// Model-level diff: generated parts by id and hardware quantities by definition.
#include "fos/project/diff.hpp"

#include <cmath>
#include <map>

namespace fos::project {
namespace {

PartRef partRef(const domain::Part& p) {
    return {p.id.str(), p.name, p.length, p.width, p.thickness, p.material.str()};
}

bool near(double a, double b, double tolerance) { return std::abs(a - b) <= tolerance; }

/// Parts of all assemblies in model order; a duplicated part id keeps its first occurrence.
std::vector<const domain::Part*> partsOf(const domain::Model& m, std::map<std::string, const domain::Part*>& index) {
    std::vector<const domain::Part*> list;
    for (const auto& a : m.assemblies)
        for (const auto& p : a.parts)
            if (index.emplace(p.id.str(), &p).second) list.push_back(&p);
    return list;
}

std::map<std::string, int> hardwareCounts(const domain::Model& m) {
    std::map<std::string, int> counts;
    for (const auto& a : m.assemblies)
        for (const auto& h : a.hardware) counts[h.definition.str()] += h.quantity;
    return counts;
}

} // namespace

ModelDiff diffModels(const domain::Model& before, const domain::Model& after, double tolerance) {
    ModelDiff d;
    std::map<std::string, const domain::Part*> ia, ib;
    const auto la = partsOf(before, ia);
    const auto lb = partsOf(after, ib);
    for (const auto* pa : la) {
        const auto it = ib.find(pa->id.str());
        if (it == ib.end()) {
            d.removed.push_back(partRef(*pa));
            continue;
        }
        const domain::Part& pb = *it->second;
        PartChange c;
        c.before = partRef(*pa);
        c.after = partRef(pb);
        c.sizeChanged = !near(pa->length, pb.length, tolerance) || !near(pa->width, pb.width, tolerance) ||
                        !near(pa->thickness, pb.thickness, tolerance);
        c.materialChanged = pa->material != pb.material;
        c.edgesChanged = pa->edges != pb.edges;
        c.grainChanged = pa->grain != pb.grain;
        c.operationsBefore = pa->operations.size();
        c.operationsAfter = pb.operations.size();
        if (c.sizeChanged || c.materialChanged || c.edgesChanged || c.grainChanged ||
            c.operationsBefore != c.operationsAfter)
            d.changed.push_back(std::move(c));
    }
    for (const auto* pb : lb)
        if (ia.count(pb->id.str()) == 0) d.added.push_back(partRef(*pb));

    const auto ha = hardwareCounts(before), hb = hardwareCounts(after);
    std::map<std::string, std::pair<int, int>> all;
    for (const auto& [id, n] : ha) all[id].first = n;
    for (const auto& [id, n] : hb) all[id].second = n;
    for (const auto& [id, n] : all)
        if (n.first != n.second) d.hardware.push_back({id, n.first, n.second});
    return d;
}

} // namespace fos::project
