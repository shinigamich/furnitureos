// SPDX-License-Identifier: AGPL-3.0-only
// Machining of a shaped part against its finished outline (contour.hpp).
#include "fos/furniture/contour.hpp"

#include "fos/geometry/curve_intersect.hpp"
#include "fos/geometry/polygon.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace fos::furniture {
namespace {

using domain::Face;
using geometry::Contour;
using geometry::PointLocation;
using geometry::Vec2;

/// Touching the outline within this distance (mm) is allowed.
constexpr double kTol = 1e-3;

std::string num(double v) {
    if (std::isfinite(v) && std::fabs(v) < 1e12) return core::formatLength(v, 3);
    char buf[32];
    std::snprintf(buf, sizeof buf, "%g", v);
    return buf;
}

enum class Fit { Inside, LeavesOutline, EntersCutout };

struct Region {
    const Contour& outer;
    const std::vector<Contour>& cutouts;
};

Fit circleFit(const Region& r, Vec2 c, double radius) {
    if (geometry::locatePoint(r.outer, c) == PointLocation::Outside) return Fit::LeavesOutline;
    for (std::size_t i = 0; i < r.outer.size(); ++i)
        if (geometry::distancePointContourSegment(c, geometry::segmentInfo(r.outer, i)) < radius - kTol)
            return Fit::LeavesOutline;
    for (const auto& cut : r.cutouts) {
        if (geometry::locatePoint(cut, c) != PointLocation::Outside) return Fit::EntersCutout;
        for (std::size_t i = 0; i < cut.size(); ++i)
            if (geometry::distancePointContourSegment(c, geometry::segmentInfo(cut, i)) < radius - kTol)
                return Fit::EntersCutout;
    }
    return Fit::Inside;
}

bool crosses(const Contour& poly, const Contour& c) {
    for (std::size_t i = 0; i < poly.size(); ++i) {
        const auto a = geometry::segmentInfo(poly, i);
        for (std::size_t j = 0; j < c.size(); ++j)
            if (geometry::intersectContourSegments(a, geometry::segmentInfo(c, j)).kind !=
                geometry::CurveIntersectionKind::None)
                return true;
    }
    return false;
}

/// `pts`: a simple polygon footprint. It is shrunk by kTol towards its centroid first, so a
/// footprint that only touches the outline (an edge hole starting on a straight side, a groove
/// running out through a box side) fits.
Fit polygonFit(const Region& r, std::vector<Vec2> pts) {
    if (pts.size() < 3) return Fit::Inside;
    Vec2 centroid;
    for (const Vec2& p : pts) centroid = centroid + p;
    centroid = centroid * (1.0 / static_cast<double>(pts.size()));
    for (Vec2& p : pts) {
        const Vec2 d = centroid - p;
        const double len = geometry::length(d);
        if (len > 2 * kTol) p = p + d * (kTol / len);
    }
    Contour poly;
    geometry::Polygon2 polygon;
    for (const Vec2& p : pts) {
        poly.vertices.push_back({p, 0.0});
        polygon.vertices.push_back(p);
    }
    for (const Vec2& p : pts)
        if (geometry::locatePoint(r.outer, p) == PointLocation::Outside) return Fit::LeavesOutline;
    if (crosses(poly, r.outer)) return Fit::LeavesOutline;
    for (const auto& cut : r.cutouts) {
        for (const Vec2& p : pts)
            if (geometry::locatePoint(cut, p) == PointLocation::Inside) return Fit::EntersCutout;
        if (crosses(poly, cut)) return Fit::EntersCutout;
        if (geometry::locatePoint(polygon, cut.vertices[0].point) != PointLocation::Outside) return Fit::EntersCutout;
    }
    return Fit::Inside;
}

/// Axis-parallel rectangle clipped to the part box; empty (no points) when nothing remains.
std::vector<Vec2> boxRect(const domain::Part& part, double x0, double y0, double x1, double y1) {
    x0 = std::max(x0, 0.0);
    y0 = std::max(y0, 0.0);
    x1 = std::min(x1, part.length);
    y1 = std::min(y1, part.width);
    if (!(x1 - x0 > kTol) || !(y1 - y0 > kTol)) return {};
    return {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
}

/// Footprint in the part's XY plane of something spanning [u0, u1] along an edge face and
/// reaching `depth` into the part from that edge.
std::vector<Vec2> edgeFootprint(const domain::Part& part, Face face, double u0, double u1, double depth) {
    const double L = part.length, W = part.width;
    switch (face) {
    case Face::L1: return boxRect(part, u0, 0, u1, depth);
    case Face::L2: return boxRect(part, u0, W - depth, u1, W);
    case Face::W1: return boxRect(part, 0, u0, depth, u1);
    case Face::W2: return boxRect(part, L - depth, u0, L, u1);
    default: return {};
    }
}

bool isEdgeFace(Face f) { return f != Face::A && f != Face::B; }

double acrossEdge(const domain::Part& part, Face f) {
    return f == Face::L1 || f == Face::L2 ? part.width : part.length;
}

struct Finding {
    Fit fit = Fit::Inside;
    std::string what;
};

Finding examine(const domain::Part& part, const Region& region, const domain::Operation& op) {
    Finding f;
    if (const auto* h = std::get_if<domain::Hole>(&op.shape)) {
        const double r = h->diameter / 2;
        f.what = "Hole d " + num(h->diameter) + " at (" + num(h->u) + ", " + num(h->v) + ") on face " +
                 domain::toString(h->face);
        if (!isEdgeFace(h->face)) f.fit = circleFit(region, {h->u, h->v}, r);
        else {
            const double depth = h->through ? acrossEdge(part, h->face) : h->depth;
            f.fit = polygonFit(region, edgeFootprint(part, h->face, h->u - r, h->u + r, depth));
        }
    } else if (const auto* g = std::get_if<domain::Groove>(&op.shape)) {
        const double hw = g->width / 2;
        f.what = "Groove (" + num(g->u1) + ", " + num(g->v1) + ") - (" + num(g->u2) + ", " + num(g->v2) + ") on face " +
                 domain::toString(g->face);
        if (isEdgeFace(g->face)) {
            const bool alongEdge = !core::approxEqual(g->u1, g->u2);
            const double u0 = alongEdge ? std::min(g->u1, g->u2) : g->u1 - hw;
            const double u1 = alongEdge ? std::max(g->u1, g->u2) : g->u1 + hw;
            f.fit = polygonFit(region, edgeFootprint(part, g->face, u0, u1, g->depth));
        } else if (core::approxEqual(g->v1, g->v2)) {
            f.fit = polygonFit(region, boxRect(part, std::min(g->u1, g->u2), g->v1 - hw, std::max(g->u1, g->u2), g->v1 + hw));
        } else if (core::approxEqual(g->u1, g->u2)) {
            f.fit = polygonFit(region, boxRect(part, g->u1 - hw, std::min(g->v1, g->v2), g->u1 + hw, std::max(g->v1, g->v2)));
        } else {
            const Vec2 a{g->u1, g->v1}, b{g->u2, g->v2};
            const Vec2 d = b - a;
            const Vec2 n = Vec2{-d.y, d.x} * (hw / geometry::length(d));
            f.fit = polygonFit(region, {a - n, b - n, b + n, a + n});
        }
    } else if (const auto* p = std::get_if<domain::Pocket>(&op.shape)) {
        f.what = "Pocket on face " + std::string(domain::toString(p->face));
        if (!isEdgeFace(p->face)) f.fit = polygonFit(region, p->contour);
        else if (!p->contour.empty()) {
            double u0 = p->contour[0].x, u1 = u0;
            for (const Vec2& q : p->contour) {
                u0 = std::min(u0, q.x);
                u1 = std::max(u1, q.x);
            }
            const double depth = p->through ? acrossEdge(part, p->face) : p->depth;
            f.fit = polygonFit(region, edgeFootprint(part, p->face, u0, u1, depth));
        }
    }
    return f;
}

} // namespace

core::Diagnostics checkOperationsInsideOutline(const domain::Part& part) {
    core::Diagnostics out;
    if (!part.shape) return out;
    const Region region{part.shape->outer, part.shape->cutouts};
    for (const auto& op : part.operations) {
        const Finding f = examine(part, region, op);
        if (f.fit == Fit::Inside) continue;
        const std::string where = f.fit == Fit::LeavesOutline
                                      ? "leaves the contoured outline (it lies in a removed corner or edge area)"
                                      : "enters a cut-out";
        out.push_back({core::Severity::Error, "furniture.contour.machining_outside",
                       f.what + " of part '" + part.name + "' (" + part.id.str() + ", operation " + op.id.str() +
                           " from " + op.source + ") " + where +
                           ". Change the contour or move the fitting; the operation cannot be machined as placed.",
                       part.id.str()});
    }
    return out;
}

} // namespace fos::furniture
