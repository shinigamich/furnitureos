// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/production/part_list.hpp"

#include "collect.hpp"
#include "fos/core/units.hpp"

namespace fos::production {

PartList partList(const domain::Model& model, const Scope& scope) {
    auto c = detail::collect(model, scope);
    PartList out;
    out.diagnostics = std::move(c.diagnostics);
    out.rows.reserve(c.parts.size());
    for (const auto& ref : c.parts) {
        PartRow r = detail::makeRow(model, ref);
        const auto& p = *ref.part;
        if (r.materialMissing)
            detail::warn(out.diagnostics, "production.material.missing",
                         p.material.empty()
                             ? "Part " + detail::describe(p) + " has no material assigned."
                             : "Part " + detail::describe(p) + " references material '" + p.material.str() +
                                   "' which is not in the model.",
                         p.id.str());
        for (auto s : domain::kAllEdges)
            if (r.edge(s).missing)
                detail::warn(out.diagnostics, "production.edgeband.missing",
                             "Edge " + std::string(domain::toString(s)) + " of part " + detail::describe(p) +
                                 " references edge band '" + r.edge(s).name +
                                 "' which is not in the model; it is treated as 0 mm thick in the cut size.",
                             p.id.str());
        if (!detail::positiveFinite(p.length) || !detail::positiveFinite(p.width) ||
            !detail::positiveFinite(p.thickness) || !detail::positiveFinite(r.cutLength) ||
            !detail::positiveFinite(r.cutWidth))
            detail::warn(out.diagnostics, "production.part.invalid_size",
                         "Part " + detail::describe(p) + " has a non-positive or non-finite size (finished " +
                             core::formatLength(p.length) + " x " + core::formatLength(p.width) + " x " +
                             core::formatLength(p.thickness) + " mm, cut " + core::formatLength(r.cutLength) +
                             " x " + core::formatLength(r.cutWidth) + " mm); it contributes 0 to area totals.",
                         p.id.str());
        out.rows.push_back(std::move(r));
    }
    return out;
}

} // namespace fos::production
