// SPDX-License-Identifier: AGPL-3.0-only
// Generator stage: parametric part outlines from the per-slot overrides (README "Contoured parts")
// plus the generator's own cut-outs (appliance niche ventilation).
#include "fos/furniture/contour.hpp"
#include "gen_context.hpp"

#include <algorithm>
#include <cstdio>
#include <limits>
#include <set>

namespace fos::furniture::detail {
namespace {

/// Exact decimal text of a millimetre value for a cut-out expression.
std::string exprNumber(double v) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.6f", v);
    return buf;
}

/// Region `c` mapped into the part frame of `part`: x / y extent of its eight corners.
CutoutSpec localCutout(const domain::Part& part, const PendingCutout& c) {
    const Transform inv = part.placement.inverse();
    double x0 = std::numeric_limits<double>::max(), y0 = x0, x1 = -x0, y1 = -x0;
    for (int k = 0; k < 8; ++k) {
        const Vec3 p{(k & 1) ? c.hi.x : c.lo.x, (k & 2) ? c.hi.y : c.lo.y, (k & 4) ? c.hi.z : c.lo.z};
        const Vec3 l = inv.applyPoint(p);
        x0 = std::min(x0, l.x);
        x1 = std::max(x1, l.x);
        y0 = std::min(y0, l.y);
        y1 = std::max(y1, l.y);
    }
    CutoutSpec out;
    out.kind = CutoutKind::Rectangle;
    out.x = exprNumber(x0);
    out.y = exprNumber(y0);
    out.length = exprNumber(x1 - x0);
    out.width = exprNumber(y1 - y0);
    return out;
}

} // namespace

core::Result<void> applyContours(Gen& g) {
    const parametric::Lookup lookup = [&g](const std::string& name) -> std::optional<double> {
        const auto it = g.values.find(name);
        if (it == g.values.end()) return std::nullopt;
        return it->second;
    };
    // Generator cut-outs are appended to the slot's override contour (or a plain rectangle).
    std::map<std::string, std::vector<CutoutSpec>> extra;
    for (const PendingCutout& c : g.cutouts) {
        const std::size_t index = g.index(c.slot);
        if (index >= g.assembly.parts.size())
            return core::makeError(core::ErrorCode::Internal,
                                   "The cut-out of " + c.owner + " refers to part slot '" + c.slot +
                                       "', which was not generated.",
                                   g.def.id.str());
        extra[c.slot].push_back(localCutout(g.assembly.parts[index], c));
    }
    // std::map / std::set: slots in sorted order, so diagnostics are deterministic.
    std::set<std::string> slots;
    for (const auto& [slot, ov] : g.def.materials.overrides)
        if (ov.contour) slots.insert(slot);
    for (const auto& [slot, list] : extra) slots.insert(slot);
    const auto& overrides = g.def.materials.overrides;
    for (const std::string& slot : slots) {
        const auto ov = overrides.find(slot);
        const bool hasOverride = ov != overrides.end() && ov->second.contour;
        const std::size_t index = g.index(slot);
        if (index >= g.assembly.parts.size()) {
            g.warn("furniture.contour.unused",
                   "A contour is set for part slot '" + slot + "' of cabinet '" + g.def.id.str() +
                       "', but the cabinet generates no part in that slot; the contour has no effect.",
                   g.def.id.str());
            continue;
        }
        ContourSpec spec = hasOverride ? *ov->second.contour : ContourSpec{};
        if (const auto e = extra.find(slot); e != extra.end())
            spec.cutouts.insert(spec.cutouts.end(), e->second.begin(), e->second.end());
        domain::Part& part = g.assembly.parts[index];
        const std::string label = "part '" + part.name + "' (" + slot + ") of cabinet '" + g.def.id.str() + "'";
        if (auto r = applyContour(part, spec, lookup, label); !r) return r.error();
        auto found = checkOperationsInsideOutline(part);
        g.diagnostics.insert(g.diagnostics.end(), found.begin(), found.end());
    }
    return {};
}

} // namespace fos::furniture::detail
