// SPDX-License-Identifier: AGPL-3.0-only
/// Checks 3 (part interference within an assembly, groove engagement) and 4 (between assemblies).
#include "detail.hpp"

#include "fos/machining/face.hpp"
#include "fos/validation/obb.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace fos::validation::detail {
namespace {
using core::Severity;
using geometry::Aabb;
using geometry::Vec3;

struct Item {
    const domain::Assembly* assembly;
    const domain::Part* part;
    std::size_t group; ///< Assembly index (pairs of the same group are skipped in cross mode).
    Obb obb;
    Aabb box;
    bool aligned;
};

/// Sweep and prune along X. Calls `narrow(a, b)` for every pair whose boxes overlap (by more
/// than `tol` on all three axes); `counted(a, b)` tells which pairs take part at all.
template <class Counted, class Narrow>
void sweep(const std::vector<Item>& items, double tol, std::size_t& broad, Counted counted, Narrow narrow) {
    std::vector<std::size_t> order(items.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::stable_sort(order.begin(), order.end(),
                     [&](std::size_t a, std::size_t b) { return items[a].box.min.x < items[b].box.min.x; });
    for (std::size_t k = 0; k < order.size(); ++k) {
        const Item& a = items[order[k]];
        for (std::size_t m = k + 1; m < order.size(); ++m) {
            const Item& b = items[order[m]];
            if (b.box.min.x >= a.box.max.x - tol) break; // sorted: no later box can overlap along X
            if (!counted(a, b)) continue;
            ++broad;
            if (!a.box.overlaps(b.box, tol)) continue;
            // Keep the original part order inside the pair for deterministic messages.
            if (order[k] < order[m]) narrow(a, b);
            else narrow(b, a);
        }
    }
}

/// The part that "penetrates": the one with the larger extent along the penetration axis
/// (a shelf end entering a side panel), ties -> the first of the pair.
std::pair<const Item*, const Item*> penetrator(const Item& a, const Item& b, Vec3 axis) {
    if (projectedExtent(b.obb, axis) > projectedExtent(a.obb, axis) + core::kLengthEps) return {&b, &a};
    return {&a, &b};
}

/// Swept volume of a straight, axis-parallel (in face coordinates) groove, in the assembly
/// frame: centre line +- width/2 across, no extension beyond the end points, depth measured
/// from the face surface. Empty when the groove is not axis-parallel or not well formed.
Aabb grooveBox(const domain::Part& p, const domain::Groove& g) {
    Aabb box;
    const double eps = core::kLengthEps;
    if (!finite(g.u1) || !finite(g.v1) || !finite(g.u2) || !finite(g.v2) || !(g.width > 0) || !(g.depth > 0) ||
        !finite(g.width) || !finite(g.depth))
        return box;
    double u0, u1, v0, v1;
    if (std::fabs(g.v1 - g.v2) <= eps) {
        u0 = std::min(g.u1, g.u2), u1 = std::max(g.u1, g.u2), v0 = g.v1 - g.width / 2, v1 = g.v1 + g.width / 2;
    } else if (std::fabs(g.u1 - g.u2) <= eps) {
        v0 = std::min(g.v1, g.v2), v1 = std::max(g.v1, g.v2), u0 = g.u1 - g.width / 2, u1 = g.u1 + g.width / 2;
    } else {
        return box;
    }
    for (int i = 0; i < 8; ++i)
        box.expand(machining::faceToAssembly(p, g.face, (i & 1) ? u1 : u0, (i & 2) ? v1 : v0, (i & 4) ? g.depth : 0.0));
    return box;
}

struct GrooveMatch {
    bool engaged = false;  ///< The overlap shares volume with a groove of one of the parts.
    bool inside = false;   ///< ... and lies completely inside it (within grooveFitTolerance).
    const Item* carrier = nullptr;
    const domain::Operation* op = nullptr;
    double excess = 0;     ///< Largest protrusion beyond the groove box.
    int excessAxis = 0;
};

GrooveMatch matchGroove(const Item& a, const Item& b, double fitTol) {
    GrooveMatch best;
    if (!a.aligned || !b.aligned) return best;
    Aabb inter;
    inter.min = {std::max(a.box.min.x, b.box.min.x), std::max(a.box.min.y, b.box.min.y),
                 std::max(a.box.min.z, b.box.min.z)};
    inter.max = {std::min(a.box.max.x, b.box.max.x), std::min(a.box.max.y, b.box.max.y),
                 std::min(a.box.max.z, b.box.max.z)};
    for (const Item* carrier : {&a, &b}) {
        for (const auto& op : carrier->part->operations) {
            const auto* g = std::get_if<domain::Groove>(&op.shape);
            if (!g) continue;
            const Aabb gb = grooveBox(*carrier->part, *g);
            if (gb.empty() || !gb.overlaps(inter)) continue;
            const double lo[3] = {gb.min.x - inter.min.x, gb.min.y - inter.min.y, gb.min.z - inter.min.z};
            const double hi[3] = {inter.max.x - gb.max.x, inter.max.y - gb.max.y, inter.max.z - gb.max.z};
            double excess = 0;
            int axis = 0;
            for (int i = 0; i < 3; ++i) {
                const double e = std::max(lo[i], hi[i]);
                if (e > excess) excess = e, axis = i;
            }
            if (!best.engaged || excess < best.excess) {
                best.engaged = true;
                best.carrier = carrier;
                best.op = &op;
                best.excess = excess;
                best.excessAxis = axis;
                best.inside = excess <= fitTol;
            }
        }
    }
    return best;
}

std::string by(const ObbOverlap& r) { return num(r.depth) + " mm along " + axisName(r.axis); }
} // namespace

void checkPartInterference(Context& c) {
    const ValidationOptions& o = c.options;
    ValidationStats& st = c.report.stats;
    const double tol = std::max(o.interferenceTolerance, core::kLengthEps);
    for (const auto& a : c.model.assemblies) {
        const std::size_t n = a.parts.size();
        st.possiblePartPairs += n * (n > 0 ? n - 1 : 0) / 2;
        std::vector<Item> items;
        for (const auto& p : a.parts) {
            if (!validSize(p) || !validPlacement(p.placement)) continue; // reported by the part check
            items.push_back({&a, &p, 0, makeObb(p.localBounds(), p.placement), p.bounds(), axisAligned(p.placement)});
        }
        auto narrow = [&](const Item& x, const Item& y) {
            ++st.partPairsTested;
            const ObbOverlap r = intersectObb(x.obb, y.obb, tol);
            if (!r.intersects) return;
            const GrooveMatch g = matchGroove(x, y, std::max(o.grooveFitTolerance, 0.0));
            if (g.inside) {
                ++st.grooveEngagementsAllowed;
                return;
            }
            const std::string aid = a.id.str();
            if (g.engaged) {
                const Item* inGroove = g.carrier == &x ? &y : &x;
                const std::string axis(1, static_cast<char>('X' + g.excessAxis));
                c.add(Severity::Error, "validation.interference.outside_groove",
                      label(*inGroove->part) + " engages groove '" + g.op->id.str() + "' of " + label(*g.carrier->part) +
                          " but sticks out of the groove by " + num(g.excess) + " mm along " + axis +
                          " (groove fit tolerance " + num(o.grooveFitTolerance) + " mm).",
                      inGroove->part->id.str(), aid, inGroove->part->id.str(),
                      {g.carrier->part->id.str(), g.op->id.str()});
                return;
            }
            const auto [pen, other] = penetrator(x, y, r.axis);
            c.add(Severity::Error, "validation.interference.parts",
                  label(*pen->part) + " penetrates " + label(*other->part) + " by " + by(r) + ".", pen->part->id.str(),
                  aid, pen->part->id.str(), {other->part->id.str()});
        };
        sweep(items, tol, st.broadPhaseChecks, [](const Item&, const Item&) { return true; }, narrow);
    }
}

void checkAssemblyInterference(Context& c) {
    const ValidationOptions& o = c.options;
    ValidationStats& st = c.report.stats;
    const double tol = std::max(o.interferenceTolerance, core::kLengthEps);
    const auto& as = c.model.assemblies;
    std::size_t before = 0;
    for (const auto& a : as) {
        st.possibleCrossAssemblyPartPairs += before * a.parts.size();
        before += a.parts.size();
    }
    std::vector<Item> items;
    for (std::size_t ai = 0; ai < as.size(); ++ai) {
        const auto& a = as[ai];
        if (!validPlacement(a.placement)) continue;
        for (const auto& p : a.parts) {
            if (!validSize(p) || !validPlacement(p.placement)) continue;
            const geometry::Transform t = a.placement * p.placement;
            items.push_back({&a, &p, ai, makeObb(p.localBounds(), t), p.localBounds().transformed(t), false});
        }
    }
    auto counted = [](const Item& x, const Item& y) { return x.group != y.group; };
    auto narrow = [&](const Item& x, const Item& y) {
        ++st.crossAssemblyPartPairsTested;
        const ObbOverlap r = intersectObb(x.obb, y.obb, tol);
        if (!r.intersects) return;
        const auto [pen, other] = penetrator(x, y, r.axis);
        c.add(o.assemblyInterferenceSeverity, "validation.interference.assemblies",
              label(*pen->part) + " of " + label(*pen->assembly) + " penetrates " + label(*other->part) + " of " +
                  label(*other->assembly) + " by " + by(r) + " (project frame).",
              pen->part->id.str(), pen->assembly->id.str(), pen->part->id.str(),
              {other->part->id.str(), other->assembly->id.str()});
    };
    sweep(items, tol, st.crossAssemblyBroadPhaseChecks, counted, narrow);
}

} // namespace fos::validation::detail
