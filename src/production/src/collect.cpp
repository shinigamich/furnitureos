// SPDX-License-Identifier: AGPL-3.0-only
#include "collect.hpp"

#include "shape.hpp"

namespace fos::production::detail {

Collected collect(const domain::Model& model, const Scope& scope, PartSet set) {
    Collected out;
    int number = 0;
    for (const auto& a : model.assemblies) {
        const bool inScope = !scope.assembly || a.id == *scope.assembly;
        if (inScope) out.assemblies.push_back(&a);
        for (const auto& p : a.parts) {
            ++number;
            if (!inScope) continue;
            if (set == PartSet::Workshop && domain::isBoughtIn(p))
                ++out.boughtInExcluded;
            else
                out.parts.push_back({&a, &p, number});
        }
    }
    if (scope.assembly && out.assemblies.empty())
        warn(out.diagnostics, "production.scope.unknown_assembly",
             "Assembly '" + scope.assembly->str() + "' is not in the model; the report is empty.",
             scope.assembly->str());
    return out;
}

void noteBoughtIn(core::Diagnostics& out, const Collected& c, const std::string& what) {
    if (c.boughtInExcluded <= 0) return;
    out.push_back({core::Severity::Info, "production.bought_in.excluded",
                   std::to_string(c.boughtInExcluded) +
                       " bought-in part(s) (glass panes, ordered cut to size) are not in the " + what +
                       "; see the bought-in items list.",
                   ""});
}

EdgeCell resolveEdge(const domain::Model& model, const std::optional<domain::EdgeBandId>& e) {
    EdgeCell cell;
    if (!e) return cell;
    cell.id = *e;
    if (const auto* eb = model.findEdgeBand(*e)) {
        cell.name = eb->name;
        cell.thickness = eb->thickness;
    } else {
        cell.name = e->str();
        cell.missing = true;
    }
    return cell;
}

PartRow makeRow(const domain::Model& model, const PartRef& ref) {
    const auto& p = *ref.part;
    PartRow r;
    r.number = ref.number;
    r.assemblyId = ref.assembly->id;
    r.assemblyName = ref.assembly->name;
    r.partId = p.id;
    r.name = p.name;
    r.role = p.role;
    r.materialId = p.material;
    if (const auto* m = model.findMaterial(p.material))
        r.materialName = m->name;
    else
        r.materialMissing = true;
    r.length = p.length;
    r.width = p.width;
    r.thickness = p.thickness;
    const auto cut = domain::cutSize(p, model);
    r.cutLength = cut.length;
    r.cutWidth = cut.width;
    r.grain = p.grain;
    for (auto s : domain::kAllEdges) r.edges[static_cast<std::size_t>(s)] = resolveEdge(model, p.edge(s));
    r.operationCount = static_cast<int>(p.operations.size());
    if (p.shape) {
        r.shaped = true;
        r.outlineAreaM2 = finishedAreaM2(p);
        r.outlineSegments = static_cast<int>(p.shape->outer.size());
        r.cutoutCount = static_cast<int>(p.shape->cutouts.size());
        for (const auto& e : p.shape->outerEdges) r.outlineEdges.push_back(resolveEdge(model, e));
    }
    return r;
}

std::string describe(const domain::Part& part) { return "'" + part.name + "' (" + part.id.str() + ")"; }

void warn(core::Diagnostics& out, std::string code, std::string message, std::string entityId) {
    out.push_back({core::Severity::Warning, std::move(code), std::move(message), std::move(entityId)});
}

} // namespace fos::production::detail
