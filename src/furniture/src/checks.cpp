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

} // namespace

std::vector<Interpenetration> findInterpenetrations(const domain::Assembly& a, double tolerance) {
    std::vector<Interpenetration> out;
    std::vector<geometry::Aabb> boxes;
    boxes.reserve(a.parts.size());
    for (const auto& p : a.parts) boxes.push_back(p.bounds());
    for (std::size_t i = 0; i < a.parts.size(); ++i)
        for (std::size_t j = i + 1; j < a.parts.size(); ++j) {
            const geometry::Aabb &bi = boxes[i], &bj = boxes[j];
            if (!bi.overlaps(bj, tolerance)) continue;
            geometry::Aabb shared;
            shared.min = {std::max(bi.min.x, bj.min.x), std::max(bi.min.y, bj.min.y), std::max(bi.min.z, bj.min.z)};
            shared.max = {std::min(bi.max.x, bj.max.x), std::min(bi.max.y, bj.max.y), std::min(bi.max.z, bj.max.z)};
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
