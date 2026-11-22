// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/furniture/checks.hpp"

#include "gen_context.hpp"

#include <algorithm>
#include <cmath>

namespace fos::furniture {
namespace {

/// Machined volume of an axis-parallel groove on face A or B, assembly frame; empty otherwise.
geometry::Aabb grooveVolume(const domain::Part& part, const domain::Groove& gr) {
    geometry::Aabb local;
    const double hw = gr.width / 2;
    if (gr.face != domain::Face::A && gr.face != domain::Face::B) return local;
    const double z0 = gr.face == domain::Face::A ? part.thickness - gr.depth : 0.0;
    const double z1 = gr.face == domain::Face::A ? part.thickness : gr.depth;
    if (core::approxEqual(gr.v1, gr.v2)) {
        local.expand(geometry::Vec3{std::min(gr.u1, gr.u2), gr.v1 - hw, z0});
        local.expand(geometry::Vec3{std::max(gr.u1, gr.u2), gr.v1 + hw, z1});
    } else if (core::approxEqual(gr.u1, gr.u2)) {
        local.expand(geometry::Vec3{gr.u1 - hw, std::min(gr.v1, gr.v2), z0});
        local.expand(geometry::Vec3{gr.u1 + hw, std::max(gr.v1, gr.v2), z1});
    }
    return local.transformed(part.placement);
}

bool contains(const geometry::Aabb& outer, const geometry::Aabb& inner, double tol) {
    return !outer.empty() && inner.min.x >= outer.min.x - tol && inner.min.y >= outer.min.y - tol &&
           inner.min.z >= outer.min.z - tol && inner.max.x <= outer.max.x + tol && inner.max.y <= outer.max.y + tol &&
           inner.max.z <= outer.max.z + tol;
}

bool insideAGroove(const domain::Part& part, const geometry::Aabb& overlap, double tol) {
    for (const auto& op : part.operations)
        if (const auto* gr = std::get_if<domain::Groove>(&op.shape))
            if (contains(grooveVolume(part, *gr), overlap, tol)) return true;
    return false;
}

/// Boxes (assembly frame) covering the solid of `part`: its bounding box, or, for a part whose
/// outer outline is RECTILINEAR (straight, axis-parallel segments only, e.g. an L-shaped
/// corner panel), one box per (x slab, y interval) of the outline. Cut-outs are ignored (they
/// only remove material, so the boxes stay a superset of the solid).
std::vector<geometry::Aabb> solidBoxes(const domain::Part& part) {
    if (!part.shape || part.shape->outer.size() < 4) return {part.bounds()};
    const auto& vs = part.shape->outer.vertices;
    const std::size_t n = vs.size();
    std::vector<double> xs;
    for (std::size_t k = 0; k < n; ++k) {
        const geometry::Vec2 a = vs[k].point, b = vs[(k + 1) % n].point;
        if (vs[k].bulge != 0.0 || (!core::approxEqual(a.x, b.x) && !core::approxEqual(a.y, b.y)))
            return {part.bounds()};
        xs.push_back(a.x);
    }
    std::sort(xs.begin(), xs.end());
    xs.erase(std::unique(xs.begin(), xs.end(), [](double l, double r) { return core::approxEqual(l, r); }), xs.end());
    std::vector<geometry::Aabb> out;
    for (std::size_t s = 0; s + 1 < xs.size(); ++s) {
        const double mid = (xs[s] + xs[s + 1]) / 2;
        std::vector<double> ys; // horizontal segments crossed by the line x = mid
        for (std::size_t k = 0; k < n; ++k) {
            const geometry::Vec2 a = vs[k].point, b = vs[(k + 1) % n].point;
            if (core::approxEqual(a.y, b.y) && std::min(a.x, b.x) < mid && std::max(a.x, b.x) > mid) ys.push_back(a.y);
        }
        std::sort(ys.begin(), ys.end());
        for (std::size_t k = 0; k + 1 < ys.size(); k += 2) {
            geometry::Aabb local;
            local.expand(geometry::Vec3{xs[s], ys[k], 0.0});
            local.expand(geometry::Vec3{xs[s + 1], ys[k + 1], part.thickness});
            out.push_back(local.transformed(part.placement));
        }
    }
    if (out.empty()) return {part.bounds()};
    return out;
}

} // namespace

std::vector<Interpenetration> findInterpenetrations(const domain::Assembly& a, double tolerance) {
    std::vector<Interpenetration> out;
    std::vector<geometry::Aabb> bounds;
    std::vector<std::vector<geometry::Aabb>> solids;
    bounds.reserve(a.parts.size());
    solids.reserve(a.parts.size());
    for (const auto& p : a.parts) {
        bounds.push_back(p.bounds());
        solids.push_back(solidBoxes(p));
    }
    for (std::size_t i = 0; i < a.parts.size(); ++i)
        for (std::size_t j = i + 1; j < a.parts.size(); ++j) {
            if (!bounds[i].overlaps(bounds[j], tolerance)) continue;
            // Shared volume = bounding box of the overlaps of the solid boxes of both parts.
            geometry::Aabb shared;
            for (const auto& bi : solids[i])
                for (const auto& bj : solids[j]) {
                    if (!bi.overlaps(bj, tolerance)) continue;
                    shared.expand(geometry::Vec3{std::max(bi.min.x, bj.min.x), std::max(bi.min.y, bj.min.y),
                                                 std::max(bi.min.z, bj.min.z)});
                    shared.expand(geometry::Vec3{std::min(bi.max.x, bj.max.x), std::min(bi.max.y, bj.max.y),
                                                 std::min(bi.max.z, bj.max.z)});
                }
            if (shared.empty()) continue;
            if (insideAGroove(a.parts[i], shared, tolerance) || insideAGroove(a.parts[j], shared, tolerance)) continue;
            out.push_back({i, j, shared});
        }
    return out;
}

std::string describe(const domain::Assembly& a, const Interpenetration& f) {
    using detail::num;
    const auto name = [&](std::size_t k) { return "'" + a.parts[k].name + "' (" + a.parts[k].id.str() + ")"; };
    const geometry::Aabb& o = f.overlap;
    const geometry::Vec3 s = o.size();
    return name(f.first) + " and " + name(f.second) + " interpenetrate by " + num(s.x) + " x " + num(s.y) + " x " +
           num(s.z) + " mm (x " + num(o.min.x) + ".." + num(o.max.x) + ", y " + num(o.min.y) + ".." + num(o.max.y) +
           ", z " + num(o.min.z) + ".." + num(o.max.z) + ")";
}

namespace detail {
void checkGeometry(Gen& g) {
    for (const auto& f : findInterpenetrations(g.assembly))
        g.error("furniture.geometry.interpenetration",
                describe(g.assembly, f) +
                    " outside any groove. Check pinned parameters and custom expressions that position these parts.",
                g.assembly.parts[f.first].id.str());
}
} // namespace detail

} // namespace fos::furniture
