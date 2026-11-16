// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/machining/material_region.hpp"

#include "fos/domain/model.hpp"
#include "fos/geometry/curve_intersect.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace fos::machining {
namespace {
using geometry::Contour;
using geometry::ContourSegmentInfo;
using geometry::Vec2;

struct Box {
    double x0, y0, x1, y1;
};

/// Conservative box of one segment (full circle box for arcs).
Box segmentBox(const ContourSegmentInfo& s) noexcept {
    if (s.isArc)
        return {s.center.x - s.radius, s.center.y - s.radius, s.center.x + s.radius, s.center.y + s.radius};
    return {std::min(s.start.x, s.end.x), std::min(s.start.y, s.end.y), std::max(s.start.x, s.end.x),
            std::max(s.start.y, s.end.y)};
}

bool boxesMeet(const Box& a, const Box& b, double eps) noexcept {
    return a.x0 <= b.x1 + eps && b.x0 <= a.x1 + eps && a.y0 <= b.y1 + eps && b.y0 <= a.y1 + eps;
}

std::vector<ContourSegmentInfo> segments(const Contour& c) {
    std::vector<ContourSegmentInfo> out;
    out.reserve(c.size());
    for (std::size_t i = 0; i < c.size(); ++i) out.push_back(geometry::segmentInfo(c, i));
    return out;
}

/// True when `p` is inside or on the material (used for the connectivity argument).
bool inClosedMaterial(const MaterialRegion& r, Vec2 p, double eps) {
    const auto loc = locateInMaterial(r, p, eps).location;
    return loc == MaterialLocation::Material || loc == MaterialLocation::Boundary;
}

} // namespace

std::optional<MaterialRegion> shapedMaterialRegion(const domain::Part& part) {
    if (!part.shape || domain::isRectangularOutline(part)) return std::nullopt;
    if (!geometry::validateContour(part.shape->outer)) return std::nullopt;
    MaterialRegion r;
    r.outer = geometry::normalizedCcw(part.shape->outer);
    for (const auto& c : part.shape->cutouts) {
        if (!geometry::validateContour(c)) return std::nullopt;
        r.cutouts.push_back(geometry::normalizedCcw(c));
    }
    return r;
}

PointInMaterial locateInMaterial(const MaterialRegion& region, Vec2 p, double eps) {
    PointInMaterial out;
    const auto outer = geometry::locatePoint(region.outer, p, eps);
    if (outer == geometry::PointLocation::Outside) return out;
    if (outer == geometry::PointLocation::OnBoundary) {
        out.location = MaterialLocation::Boundary;
        return out;
    }
    for (std::size_t k = 0; k < region.cutouts.size(); ++k) {
        const auto loc = geometry::locatePoint(region.cutouts[k], p, eps);
        if (loc == geometry::PointLocation::Outside) continue;
        out.cutout = k;
        out.onCutout = true;
        out.location = loc == geometry::PointLocation::Inside ? MaterialLocation::InCutout : MaterialLocation::Boundary;
        return out;
    }
    out.location = MaterialLocation::Material;
    return out;
}

BoundaryDistance distanceToBoundary(const MaterialRegion& region, Vec2 p) {
    BoundaryDistance best;
    best.distance = std::numeric_limits<double>::infinity();
    auto scan = [&](const Contour& c, bool cut, std::size_t k) {
        for (std::size_t i = 0; i < c.size(); ++i) {
            const double d = geometry::distancePointContourSegment(p, geometry::segmentInfo(c, i));
            if (d < best.distance) best = {d, cut, k, i};
        }
    };
    scan(region.outer, false, 0);
    for (std::size_t k = 0; k < region.cutouts.size(); ++k) scan(region.cutouts[k], true, k);
    return best;
}

bool contoursTouch(const Contour& a, const Contour& b, double eps) {
    const auto rb = geometry::bounds(a), rc = geometry::bounds(b);
    if (!boxesMeet({rb.min.x, rb.min.y, rb.max.x, rb.max.y}, {rc.min.x, rc.min.y, rc.max.x, rc.max.y}, eps))
        return false;
    const auto sa = segments(a), sb = segments(b);
    std::vector<Box> bb;
    bb.reserve(sb.size());
    for (const auto& s : sb) bb.push_back(segmentBox(s));
    for (const auto& x : sa) {
        const Box bx = segmentBox(x);
        for (std::size_t j = 0; j < sb.size(); ++j) {
            if (!boxesMeet(bx, bb[j], eps)) continue;
            if (geometry::intersectContourSegments(x, sb[j], eps).kind != geometry::CurveIntersectionKind::None)
                return true;
        }
    }
    return false;
}

bool regionsIntersect(const MaterialRegion& a, const MaterialRegion& b, double eps) {
    if (a.outer.size() == 0 || b.outer.size() == 0) return false;
    std::vector<const Contour*> ca{&a.outer}, cb{&b.outer};
    for (const auto& c : a.cutouts) ca.push_back(&c);
    for (const auto& c : b.cutouts) cb.push_back(&c);
    for (const Contour* x : ca)
        for (const Contour* y : cb)
            if (contoursTouch(*x, *y, eps)) return true;
    // No boundary contact: each (connected) region lies entirely inside or entirely outside
    // the other one, so one point of each decides.
    return inClosedMaterial(b, a.outer.vertices.front().point, eps) ||
           inClosedMaterial(a, b.outer.vertices.front().point, eps);
}

FootprintResult classifyFootprint(const MaterialRegion& region, const Contour& footprint, double eps) {
    FootprintResult r;
    if (footprint.size() == 0) return r;
    if (contoursTouch(footprint, region.outer, eps)) {
        r.placement = FootprintPlacement::CrossesOutline;
        return r;
    }
    for (std::size_t k = 0; k < region.cutouts.size(); ++k)
        if (contoursTouch(footprint, region.cutouts[k], eps)) {
            r.placement = FootprintPlacement::CrossesCutout;
            r.cutout = k;
            return r;
        }
    const PointInMaterial p = locateInMaterial(region, footprint.vertices.front().point, eps);
    if (p.location == MaterialLocation::OutsideOutline) {
        r.placement = FootprintPlacement::OutsideOutline;
        return r;
    }
    if (p.location == MaterialLocation::InCutout) {
        r.placement = FootprintPlacement::InCutout;
        r.cutout = p.cutout;
        return r;
    }
    for (std::size_t k = 0; k < region.cutouts.size(); ++k)
        if (geometry::locatePoint(footprint, region.cutouts[k].vertices.front().point, eps) ==
            geometry::PointLocation::Inside) {
            r.placement = FootprintPlacement::EnclosesCutout;
            r.cutout = k;
            return r;
        }
    return r;
}

std::string describe(const FootprintResult& r) {
    const std::string k = std::to_string(r.cutout); // 0-based, as PartShape::cutouts
    switch (r.placement) {
    case FootprintPlacement::Inside: return "lies inside the material";
    case FootprintPlacement::CrossesOutline: return "crosses the shaped outline";
    case FootprintPlacement::CrossesCutout: return "crosses cut-out " + k + " of the shaped outline";
    case FootprintPlacement::EnclosesCutout: return "encloses cut-out " + k + " of the shaped outline";
    case FootprintPlacement::OutsideOutline: return "lies outside the shaped outline";
    case FootprintPlacement::InCutout: return "lies inside cut-out " + k + " of the shaped outline";
    }
    return {};
}

} // namespace fos::machining
