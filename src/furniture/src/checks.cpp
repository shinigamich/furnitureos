// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/furniture/checks.hpp"

#include "gen_context.hpp"

#include <algorithm>
#include <cmath>

namespace fos::furniture {
namespace {

/// Machined volumes (assembly frame) of the axis-parallel grooves of `part`.
std::vector<geometry::Aabb> grooveVolumes(const domain::Part& part) {
    std::vector<geometry::Aabb> out;
    for (const auto& op : part.operations)
        if (const auto* gr = std::get_if<domain::Groove>(&op.shape)) {
            const geometry::Aabb local = detail::grooveBoxLocal(part, *gr);
            if (!local.empty()) out.push_back(local.transformed(part.placement));
        }
    return out;
}

/// `boxes` minus `cut` (each box split into at most 6 pieces).
std::vector<geometry::Aabb> subtract(const std::vector<geometry::Aabb>& boxes, const geometry::Aabb& cut) {
    std::vector<geometry::Aabb> out;
    for (geometry::Aabb b : boxes) {
        if (!b.overlaps(cut, 0.0)) {
            out.push_back(b);
            continue;
        }
        double* lo[3] = {&b.min.x, &b.min.y, &b.min.z};
        double* hi[3] = {&b.max.x, &b.max.y, &b.max.z};
        const double clo[3] = {cut.min.x, cut.min.y, cut.min.z}, chi[3] = {cut.max.x, cut.max.y, cut.max.z};
        for (int k = 0; k < 3; ++k) {
            if (*lo[k] < clo[k]) { // slab below the cut along axis k
                geometry::Aabb piece = b;
                double* phi[3] = {&piece.max.x, &piece.max.y, &piece.max.z};
                *phi[k] = clo[k];
                out.push_back(piece);
                *lo[k] = clo[k];
            }
            if (*hi[k] > chi[k]) {
                geometry::Aabb piece = b;
                double* plo[3] = {&piece.min.x, &piece.min.y, &piece.min.z};
                *plo[k] = chi[k];
                out.push_back(piece);
                *hi[k] = chi[k];
            }
        }
    }
    return out;
}

/// True when `shared` lies (within `tol`) inside the union of the grooves of both parts:
/// every point of it is machined away from at least one of them.
bool coveredByGrooves(const domain::Part& a, const domain::Part& b, const geometry::Aabb& shared, double tol) {
    std::vector<geometry::Aabb> rest{shared};
    for (const domain::Part* p : {&a, &b})
        for (geometry::Aabb gv : grooveVolumes(*p)) {
            gv.min = gv.min - geometry::Vec3{tol, tol, tol};
            gv.max = gv.max + geometry::Vec3{tol, tol, tol};
            rest = subtract(rest, gv);
            if (rest.empty()) return true;
        }
    for (const auto& r : rest) {
        const geometry::Vec3 s = r.size();
        if (s.x > tol && s.y > tol && s.z > tol) return false;
    }
    return true;
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
            if (coveredByGrooves(a.parts[i], a.parts[j], shared, tolerance)) continue;
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
geometry::Aabb grooveBoxLocal(const domain::Part& part, const domain::Groove& gr) {
    geometry::Aabb local;
    const double hw = gr.width / 2, L = part.length, W = part.width, T = part.thickness, d = gr.depth;
    // Rectangle of the groove in face coordinates (u, v).
    double u0, u1, v0, v1;
    if (core::approxEqual(gr.v1, gr.v2)) {
        u0 = std::min(gr.u1, gr.u2), u1 = std::max(gr.u1, gr.u2), v0 = gr.v1 - hw, v1 = gr.v1 + hw;
    } else if (core::approxEqual(gr.u1, gr.u2)) {
        u0 = gr.u1 - hw, u1 = gr.u1 + hw, v0 = std::min(gr.v1, gr.v2), v1 = std::max(gr.v1, gr.v2);
    } else {
        return local;
    }
    using geometry::Vec3;
    switch (gr.face) {
    case domain::Face::A: local.expand(Vec3{u0, v0, T - d}), local.expand(Vec3{u1, v1, T}); break;
    case domain::Face::B: local.expand(Vec3{u0, v0, 0.0}), local.expand(Vec3{u1, v1, d}); break;
    case domain::Face::L1: local.expand(Vec3{u0, 0.0, v0}), local.expand(Vec3{u1, d, v1}); break;
    case domain::Face::L2: local.expand(Vec3{u0, W - d, v0}), local.expand(Vec3{u1, W, v1}); break;
    case domain::Face::W1: local.expand(Vec3{0.0, u0, v0}), local.expand(Vec3{d, u1, v1}); break;
    case domain::Face::W2: local.expand(Vec3{L - d, u0, v0}), local.expand(Vec3{L, u1, v1}); break;
    }
    return local;
}

void checkGeometry(Gen& g) {
    for (const auto& f : findInterpenetrations(g.assembly))
        g.error("furniture.geometry.interpenetration",
                describe(g.assembly, f) +
                    " outside any groove. Check pinned parameters and custom expressions that position these parts.",
                g.assembly.parts[f.first].id.str());
}
} // namespace detail

} // namespace fos::furniture
