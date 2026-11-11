// SPDX-License-Identifier: AGPL-3.0-only
#include "collect.hpp"

namespace fos::production::detail {

Collected collect(const domain::Model& model, const Scope& scope) {
    Collected out;
    int number = 0;
    for (const auto& a : model.assemblies) {
        const bool inScope = !scope.assembly || a.id == *scope.assembly;
        if (inScope) out.assemblies.push_back(&a);
        for (const auto& p : a.parts) {
            ++number;
            if (inScope) out.parts.push_back({&a, &p, number});
        }
    }
    if (scope.assembly && out.assemblies.empty())
        warn(out.diagnostics, "production.scope.unknown_assembly",
             "Assembly '" + scope.assembly->str() + "' is not in the model; the report is empty.",
             scope.assembly->str());
    return out;
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
    for (auto s : domain::kAllEdges) {
        auto& cell = r.edges[static_cast<std::size_t>(s)];
        const auto& e = p.edge(s);
        if (!e) continue;
        cell.id = *e;
        if (const auto* eb = model.findEdgeBand(*e)) {
            cell.name = eb->name;
            cell.thickness = eb->thickness;
        } else {
            cell.name = e->str();
            cell.missing = true;
        }
    }
    r.operationCount = static_cast<int>(p.operations.size());
    return r;
}

std::string describe(const domain::Part& part) { return "'" + part.name + "' (" + part.id.str() + ")"; }

void warn(core::Diagnostics& out, std::string code, std::string message, std::string entityId) {
    out.push_back({core::Severity::Warning, std::move(code), std::move(message), std::move(entityId)});
}

} // namespace fos::production::detail
