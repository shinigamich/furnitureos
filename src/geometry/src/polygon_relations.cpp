// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/geometry/polygon.hpp"

#include <algorithm>
#include <limits>

namespace fos::geometry {

namespace {

/// Splits every edge of `p` at its intersections with the boundary of `q` and returns true
/// as soon as `pred(midpoint)` holds for one of the resulting pieces (pieces shorter than
/// eps are skipped). Deterministic order: edges of p in order, pieces from a to b.
template <class Pred>
bool anyBoundaryPiece(const Polygon2& p, const Polygon2& q, double eps, Pred pred) {
    std::vector<double> ts;
    for (std::size_t i = 0; i < p.size(); ++i) {
        const Segment2 e = p.edge(i);
        const Vec2 d = e.b - e.a;
        const double dd = dot(d, d);
        if (dd == 0.0) continue;
        const double len = std::sqrt(dd);
        ts.assign({0.0, 1.0});
        const auto addParam = [&](Vec2 x) { ts.push_back(std::clamp(dot(x - e.a, d) / dd, 0.0, 1.0)); };
        for (std::size_t j = 0; j < q.size(); ++j) {
            const SegmentIntersection x = intersectSegments(e, q.edge(j), eps);
            if (x.kind == SegmentIntersectionKind::None) continue;
            addParam(x.p0);
            if (x.kind == SegmentIntersectionKind::Overlap) addParam(x.p1);
        }
        std::sort(ts.begin(), ts.end());
        for (std::size_t k = 0; k + 1 < ts.size(); ++k) {
            if ((ts[k + 1] - ts[k]) * len <= eps) continue;
            if (pred(e.a + d * ((ts[k] + ts[k + 1]) * 0.5))) return true;
        }
    }
    return false;
}

/// A point strictly inside a simple polygon: midpoint of the widest interior span on a
/// horizontal line halfway between the two most separated consecutive vertex heights.
/// Returns false when the polygon has no vertical extent.
bool interiorPoint(const Polygon2& p, Vec2& out) {
    std::vector<double> ys;
    ys.reserve(p.size());
    for (Vec2 v : p.vertices) ys.push_back(v.y);
    std::sort(ys.begin(), ys.end());
    double bestGap = 0.0, y = 0.0;
    for (std::size_t i = 0; i + 1 < ys.size(); ++i)
        if (ys[i + 1] - ys[i] > bestGap) {
            bestGap = ys[i + 1] - ys[i];
            y = (ys[i] + ys[i + 1]) * 0.5;
        }
    if (bestGap <= 0.0) return false;
    std::vector<double> xs;
    const std::size_t n = p.size();
    for (std::size_t i = 0; i < n; ++i) {
        const Vec2 a = p[i], b = p[(i + 1) % n];
        if ((a.y > y) == (b.y > y)) continue;
        xs.push_back(a.x + (y - a.y) / (b.y - a.y) * (b.x - a.x));
    }
    std::sort(xs.begin(), xs.end());
    double bestSpan = -1.0;
    for (std::size_t i = 0; i + 1 < xs.size(); i += 2)
        if (xs[i + 1] - xs[i] > bestSpan) {
            bestSpan = xs[i + 1] - xs[i];
            out = {(xs[i] + xs[i + 1]) * 0.5, y};
        }
    return bestSpan > 0.0;
}

} // namespace

bool polygonsOverlap(const Polygon2& a, const Polygon2& b, double eps) {
    if (a.size() < 3 || b.size() < 3) return false;
    if (!rectsOverlap(bounds(a), bounds(b), 0.0, eps)) return false;
    // Some stretch of one boundary runs through the interior of the other polygon.
    if (anyBoundaryPiece(a, b, eps, [&](Vec2 m) { return locatePoint(b, m, eps) == PointLocation::Inside; }))
        return true;
    if (anyBoundaryPiece(b, a, eps, [&](Vec2 m) { return locatePoint(a, m, eps) == PointLocation::Inside; }))
        return true;
    // Otherwise the interiors are disjoint, or the boundaries coincide (same region).
    Vec2 inner;
    return interiorPoint(a, inner) && locatePoint(b, inner, eps) == PointLocation::Inside;
}

bool polygonContainsPolygon(const Polygon2& outer, const Polygon2& inner, double eps) {
    if (outer.size() < 3 || inner.empty()) return false;
    for (Vec2 v : inner.vertices)
        if (locatePoint(outer, v, eps) == PointLocation::Outside) return false;
    // No stretch of the inner boundary may leave the outer polygon (it can between two
    // contained vertices when the outer polygon is concave).
    return !anyBoundaryPiece(inner, outer, eps,
                             [&](Vec2 m) { return locatePoint(outer, m, eps) == PointLocation::Outside; });
}

bool polygonContainsPolygon(const PolygonWithHoles& outer, const Polygon2& inner, double eps) {
    if (!polygonContainsPolygon(outer.outer, inner, eps)) return false;
    for (const Polygon2& h : outer.holes)
        if (polygonsOverlap(h, inner, eps)) return false;
    return true;
}

double polygonDistance(const Polygon2& a, const Polygon2& b) {
    if (a.empty() || b.empty()) return std::numeric_limits<double>::infinity();
    double best = std::numeric_limits<double>::infinity();
    for (std::size_t i = 0; i < a.size(); ++i)
        for (std::size_t j = 0; j < b.size(); ++j) {
            best = std::min(best, closestPointsSegments(a.edge(i), b.edge(j)).distance);
            if (best == 0.0) return 0.0;
        }
    // Boundaries are disjoint: either the polygons are apart or one is inside the other.
    if (locatePoint(b, a[0], 0.0) != PointLocation::Outside) return 0.0;
    if (locatePoint(a, b[0], 0.0) != PointLocation::Outside) return 0.0;
    return best;
}

} // namespace fos::geometry
