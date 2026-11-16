// SPDX-License-Identifier: AGPL-3.0-only
// Generator stage: parametric part outlines from the per-slot overrides (README "Contoured parts").
#include "fos/furniture/contour.hpp"
#include "gen_context.hpp"

namespace fos::furniture::detail {

core::Result<void> applyContours(Gen& g) {
    const parametric::Lookup lookup = [&g](const std::string& name) -> std::optional<double> {
        const auto it = g.values.find(name);
        if (it == g.values.end()) return std::nullopt;
        return it->second;
    };
    // std::map: slots in sorted order, so diagnostics are deterministic.
    for (const auto& [slot, ov] : g.def.materials.overrides) {
        if (!ov.contour) continue;
        const std::size_t index = g.index(slot);
        if (index >= g.assembly.parts.size()) {
            g.warn("furniture.contour.unused",
                   "A contour is set for part slot '" + slot + "' of cabinet '" + g.def.id.str() +
                       "', but the cabinet generates no part in that slot; the contour has no effect.",
                   g.def.id.str());
            continue;
        }
        domain::Part& part = g.assembly.parts[index];
        const std::string label = "part '" + part.name + "' (" + slot + ") of cabinet '" + g.def.id.str() + "'";
        if (auto r = applyContour(part, *ov.contour, lookup, label); !r) return r.error();
        auto found = checkOperationsInsideOutline(part);
        g.diagnostics.insert(g.diagnostics.end(), found.begin(), found.end());
    }
    return {};
}

} // namespace fos::furniture::detail
