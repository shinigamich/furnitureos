// SPDX-License-Identifier: AGPL-3.0-only
/// Exact-outline interference of contoured parts (ADR-0008), README check 3.
///
/// Criterion. Two solids X and Y "share material thicker than d" when the erosions
/// X (-) B(d/2) and Y (-) B(d/2) (B = ball) still intersect. For d = interferenceTolerance this
/// is the shaped-part analogue of the box rule "overlap > tolerance"; the largest such d (found
/// by bisection) is the diameter of the largest ball that fits into the shared material and is
/// reported as the depth. Erosions are exact:
///   * a part is the prism R x [0, T] (R = outline minus cut-outs in the part XY plane), and
///     its erosion is erode(R, h) x [h, T - h]; erode(R, h) = outer offset inwards by h, cut-outs
///     offset outwards by h (geometry::offsetContour: lines stay lines, arcs stay arcs);
///   * a box shrinks by h on every side.
/// Shaped part X against box Y (any relative orientation): Y's eroded box is clipped to X's
/// eroded slab and projected onto X's XY plane, giving a convex polygon P; the solids share
/// material iff erode(R) and P intersect (exact, because the box is convex and X is a prism).
/// Two shaped parts with parallel thickness axes: both regions in X's plane, plus overlap of the
/// eroded thickness intervals (exact). Two shaped parts at another relative orientation: each
/// shape is tested against the other part's BOX and the pair interferes only if both tests
/// say so (conservative: it may report a pair whose exact shapes are disjoint, never the
/// opposite).
#include "shape_interference.hpp"

#include "fos/domain/model.hpp"
#include "fos/geometry/contour.hpp"
#include "fos/geometry/polygon.hpp"

#include <algorithm>
#include <cmath>

namespace fos::validation::detail {
namespace {
using geometry::Contour;
using geometry::Transform;
using geometry::Vec2;
using geometry::Vec3;
using machining::MaterialRegion;

/// Precision of the depth bisection, mm.
constexpr double kDepthPrecision = 1e-3;
/// Contact distance for the 2D boundary tests, mm.
constexpr double kContactEps = 1e-9;

/// erode(R, h); nullopt when an offset is not representable (arc radius <= h, vanishing
/// segment, self-intersection).
std::optional<MaterialRegion> erode(const MaterialRegion& r, double h) {
    if (h <= 0) return r;
    MaterialRegion out;
    auto outer = geometry::offsetContour(r.outer, -h);
    if (!outer) return std::nullopt;
    out.outer = outer.value();
    for (const auto& c : r.cutouts) {
        auto grown = geometry::offsetContour(c, h);
        if (!grown) return std::nullopt;
        out.cutouts.push_back(grown.value());
    }
    return out;
}

Contour contourOf(const geometry::Polygon2& p) {
    Contour c;
    for (const Vec2 v : p.vertices) c.vertices.push_back({v, 0.0});
    return c;
}

/// Shaped part X (region `rx`, thickness `tx`, frame `fx`) against the box of part `y` in
/// frame `fy`, both eroded by h. nullopt when erode(rx, h) is not representable.
std::optional<bool> shapeVsBox(const MaterialRegion& rx, double tx, const Transform& fx, const domain::Part& y,
                               const Transform& fy, double h) {
    const double z0 = h, z1 = tx - h;
    const double bx0 = h, bx1 = y.length - h, by0 = h, by1 = y.width - h, bz0 = h, bz1 = y.thickness - h;
    if (!(z1 > z0) || !(bx1 > bx0) || !(by1 > by0) || !(bz1 > bz0)) return false;
    const Transform m = fx.inverse() * fy; // y's part frame -> x's part frame
    Vec3 corner[8];
    for (int i = 0; i < 8; ++i)
        corner[i] = m.applyPoint({(i & 1) ? bx1 : bx0, (i & 2) ? by1 : by0, (i & 4) ? bz1 : bz0});
    std::vector<Vec2> pts;
    for (const Vec3& c : corner)
        if (c.z >= z0 && c.z <= z1) pts.push_back({c.x, c.y});
    for (int i = 0; i < 8; ++i)
        for (int bit : {1, 2, 4}) {
            const int j = i | bit;
            if (j == i) continue; // each edge once: i has the bit clear
            const Vec3 a = corner[i], b = corner[j];
            for (const double z : {z0, z1}) {
                if ((a.z - z) * (b.z - z) >= 0) continue;
                const double t = (z - a.z) / (b.z - a.z);
                pts.push_back({a.x + t * (b.x - a.x), a.y + t * (b.y - a.y)});
            }
        }
    const geometry::Polygon2 hull = geometry::convexHull(pts, kContactEps);
    if (hull.size() < 3 || !(geometry::area(hull) > 1e-12)) return false; // no volume in the slab
    const auto er = erode(rx, h);
    if (!er) return std::nullopt;
    MaterialRegion p;
    p.outer = contourOf(hull);
    return machining::regionsIntersect(*er, p, kContactEps);
}

/// `r` (part frame of b) mapped into the part frame of a by the rigid `m` whose z axis is
/// (anti)parallel to a's: a mirror image in 2D when anti-parallel (bulges change sign).
MaterialRegion mapRegion(const MaterialRegion& r, const Transform& m) {
    const bool mirrored = m.xAxis.x * m.yAxis.y - m.xAxis.y * m.yAxis.x < 0;
    auto map = [&](const Contour& c) {
        Contour out;
        for (const auto& v : c.vertices) {
            const Vec3 p = m.applyPoint({v.point.x, v.point.y, 0});
            out.vertices.push_back({{p.x, p.y}, mirrored ? -v.bulge : v.bulge});
        }
        return geometry::normalizedCcw(out);
    };
    MaterialRegion out;
    out.outer = map(r.outer);
    for (const auto& c : r.cutouts) out.cutouts.push_back(map(c));
    return out;
}

struct Pair {
    const domain::Part* a = nullptr;
    const domain::Part* b = nullptr;
    Transform ta, tb;
    const ShapeSolid* sa = nullptr;
    const ShapeSolid* sb = nullptr;
    bool parallel = false;      ///< Both shaped and thickness axes (anti)parallel.
    MaterialRegion bInA;        ///< parallel: b's region in a's part frame.
    double bz0 = 0, bz1 = 0;    ///< parallel: b's thickness interval along a's z.

    std::optional<bool> test(double h) const {
        if (parallel) {
            if (!(std::min(sa->thickness - h, bz1 - h) > std::max(h, bz0 + h))) return false;
            const auto ea = erode(sa->region, h);
            const auto eb = erode(bInA, h);
            if (!ea || !eb) return std::nullopt;
            return machining::regionsIntersect(*ea, *eb, kContactEps);
        }
        bool hit = true;
        if (sa) {
            const auto r = shapeVsBox(sa->region, sa->thickness, ta, *b, tb, h);
            if (!r) return std::nullopt;
            hit = *r;
        }
        if (hit && sb) {
            const auto r = shapeVsBox(sb->region, sb->thickness, tb, *a, ta, h);
            if (!r) return std::nullopt;
            hit = *r;
        }
        return hit;
    }
};

} // namespace

std::optional<ShapeSolid> makeShapeSolid(const domain::Part& part) {
    auto region = machining::shapedMaterialRegion(part);
    if (!region) return std::nullopt;
    const auto box = geometry::bounds(region->outer);
    const double e = core::kLengthEps;
    if (box.min.x < -e || box.min.y < -e || box.max.x > part.length + e || box.max.y > part.width + e)
        return std::nullopt; // the part box would not enclose the material; keep the box test
    return ShapeSolid{std::move(*region), part.thickness};
}

ShapeOverlap refineShapeOverlap(const domain::Part& a, const Transform& ta, const ShapeSolid* sa,
                                const domain::Part& b, const Transform& tb, const ShapeSolid* sb, double tol,
                                double boxDepth) {
    ShapeOverlap out;
    Pair pr;
    pr.a = &a;
    pr.b = &b;
    pr.ta = ta;
    pr.tb = tb;
    pr.sa = sa;
    pr.sb = sb;
    if (sa && sb) {
        const Transform m = ta.inverse() * tb;
        if (std::fabs(std::fabs(m.zAxis.z) - 1.0) <= 1e-9) {
            pr.parallel = true;
            pr.bInA = mapRegion(sb->region, m);
            const double z = m.origin.z, t = m.zAxis.z > 0 ? b.thickness : -b.thickness;
            pr.bz0 = std::min(z, z + t);
            pr.bz1 = std::max(z, z + t);
        }
    }
    const double h0 = tol / 2;
    const auto base = pr.test(h0);
    if (!base) return out; // undecided: caller keeps the box result
    out.decided = true;
    out.intersects = *base;
    if (!out.intersects) return out;

    // Largest erosion that still leaves shared material, by bisection on [h0, boxDepth / 2].
    double lo = h0, hi = std::max(boxDepth / 2, h0);
    if (const auto top = pr.test(hi); top && *top) {
        out.depth = boxDepth;
        return out;
    }
    for (int i = 0; i < 60 && hi - lo > kDepthPrecision / 2; ++i) {
        const double mid = (lo + hi) / 2;
        const auto r = pr.test(mid);
        if (!r) out.depthIsLowerBound = true;
        if (r && *r) lo = mid;
        else hi = mid;
    }
    out.depth = std::min(2 * lo, boxDepth);
    return out;
}

} // namespace fos::validation::detail
