// SPDX-License-Identifier: AGPL-3.0-only
// importPanelShape: imported loops -> outline + cut-outs in the part frame.
#include "dxf_internal.hpp"

#include "fos/geometry/curve_intersect.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <numeric>

namespace fos::formats {
namespace {

using dxf::num;
using geometry::Contour;
using geometry::Vec2;

constexpr std::size_t kMaxLoops = 1000;
constexpr std::size_t kMaxTotalVertices = 20000;

/// Points that must all lie inside a container for `c` to be inside it: vertices and arc midpoints.
std::vector<Vec2> probePoints(const Contour& c) {
    std::vector<Vec2> pts;
    for (std::size_t i = 0; i < c.size(); ++i) {
        pts.push_back(c.vertices[i].point);
        if (c.vertices[i].bulge == 0.0) continue;
        const auto s = geometry::segmentInfo(c, i);
        const double a = (s.startAngle + s.sweep / 2) * std::numbers::pi / 180;
        pts.push_back({s.center.x + s.radius * std::cos(a), s.center.y + s.radius * std::sin(a)});
    }
    return pts;
}

bool boundariesTouch(const Contour& a, const Contour& b) {
    const auto ba = geometry::bounds(a), bb = geometry::bounds(b);
    if (ba.max.x < bb.min.x - 1e-6 || bb.max.x < ba.min.x - 1e-6 || ba.max.y < bb.min.y - 1e-6 ||
        bb.max.y < ba.min.y - 1e-6)
        return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto sa = geometry::segmentInfo(a, i);
        for (std::size_t j = 0; j < b.size(); ++j)
            if (geometry::intersectContourSegments(sa, geometry::segmentInfo(b, j)).kind !=
                geometry::CurveIntersectionKind::None)
                return true;
    }
    return false;
}

/// `inner` lies strictly inside `outer` (no contact).
bool strictlyInside(const Contour& inner, const Contour& outer) {
    for (const Vec2 p : probePoints(inner))
        if (geometry::locatePoint(outer, p) != geometry::PointLocation::Inside) return false;
    return !boundariesTouch(inner, outer);
}

/// Some point of `a` lies inside `b`, or the boundaries touch.
bool overlaps(const Contour& a, const Contour& b) {
    for (const Vec2 p : probePoints(a))
        if (geometry::locatePoint(b, p) != geometry::PointLocation::Outside) return true;
    for (const Vec2 p : probePoints(b))
        if (geometry::locatePoint(a, p) != geometry::PointLocation::Outside) return true;
    return boundariesTouch(a, b);
}

double snap(double v) {
    if (std::fabs(v) < 1e-9) return 0.0;
    const double r = std::round(v * 1e6) / 1e6;
    return std::fabs(r - v) < 1e-9 ? r : v;
}

Contour moved(const Contour& c, Vec2 offset) {
    Contour out = geometry::translated(c, offset);
    for (auto& v : out.vertices) v.point = {snap(v.point.x), snap(v.point.y)};
    return out;
}

bool axisParallel(Vec2 a, Vec2 b) { return std::fabs(a.x - b.x) <= 1e-9 || std::fabs(a.y - b.y) <= 1e-9; }

CutoutPrimitive classify(const Contour& c) {
    CutoutPrimitive k;
    const std::size_t n = c.size();
    const auto box = geometry::bounds(c);
    // Circle: every segment an arc of the same circle, 360 degrees in total.
    {
        bool circle = true;
        double sweep = 0;
        const auto s0 = geometry::segmentInfo(c, 0);
        for (std::size_t i = 0; i < n && circle; ++i) {
            const auto s = geometry::segmentInfo(c, i);
            circle = s.isArc && geometry::length(s.center - s0.center) <= 1e-6 && std::fabs(s.radius - s0.radius) <= 1e-6;
            sweep += s.sweep;
        }
        if (circle && std::fabs(sweep - 360.0) <= 1e-6) {
            k.kind = CutoutPrimitive::Kind::Circle;
            k.x = s0.center.x;
            k.y = s0.center.y;
            k.radius = s0.radius;
            return k;
        }
    }
    const auto asRect = [&](double r) {
        k.kind = CutoutPrimitive::Kind::Rectangle;
        k.x = box.min.x;
        k.y = box.min.y;
        k.length = box.max.x - box.min.x;
        k.width = box.max.y - box.min.y;
        k.radius = r;
        return k;
    };
    if (n == 4) {
        bool rect = true;
        for (std::size_t i = 0; i < 4 && rect; ++i)
            rect = c.vertices[i].bulge == 0.0 && axisParallel(c.vertices[i].point, c.vertices[(i + 1) % 4].point);
        if (rect) return asRect(0);
    }
    if (n == 8) {
        const std::size_t first = c.vertices[0].bulge == 0.0 ? 1 : 0; // index of the first arc
        double r = -1;
        bool rounded = true;
        for (std::size_t i = 0; i < 8 && rounded; ++i) {
            const auto s = geometry::segmentInfo(c, i);
            if ((i % 2) == first % 2) {
                rounded = s.isArc && std::fabs(s.sweep - 90.0) <= 1e-6 && (r < 0 || std::fabs(s.radius - r) <= 1e-6);
                r = s.radius;
            } else {
                rounded = !s.isArc && axisParallel(s.start, s.end);
            }
        }
        if (rounded) return asRect(r);
    }
    return k;
}

std::string describe(const Contour& c) {
    const auto b = geometry::bounds(c);
    return "(" + num(b.min.x) + ", " + num(b.min.y) + ") .. (" + num(b.max.x) + ", " + num(b.max.y) + ")";
}

} // namespace

core::Result<PanelShapeImport> importPanelShape(const std::vector<Contour>& loops) {
    if (loops.empty())
        return core::makeError(core::ErrorCode::InvalidArgument, "No closed outline was found to make a panel from.");
    std::size_t total = 0;
    for (const auto& l : loops) total += l.size();
    if (loops.size() > kMaxLoops || total > kMaxTotalVertices)
        return core::makeError(core::ErrorCode::Unsupported,
                               std::to_string(loops.size()) + " outlines with " + std::to_string(total) +
                                   " segments are more than a panel supports (" + std::to_string(kMaxLoops) +
                                   " outlines, " + std::to_string(kMaxTotalVertices) + " segments).");
    std::vector<Contour> ccw;
    std::vector<double> area;
    for (const auto& l : loops) {
        ccw.push_back(geometry::normalizedCcw(l));
        area.push_back(geometry::signedArea(ccw.back()));
    }
    std::size_t outer = 0;
    for (std::size_t i = 1; i < ccw.size(); ++i)
        if (area[i] > area[outer]) outer = i;

    PanelShapeImport result;
    std::vector<std::size_t> order(ccw.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) { return area[a] > area[b]; });
    std::vector<std::size_t> accepted;
    for (std::size_t i : order) {
        if (i == outer) continue;
        std::string why;
        if (!strictlyInside(ccw[i], ccw[outer])) why = "it is outside the outline or touches it";
        for (std::size_t j : accepted)
            if (why.empty() && overlaps(ccw[i], ccw[j]))
                why = "it lies inside, touches or crosses the cut-out " + describe(ccw[j]) +
                      " (islands and overlapping cut-outs are not supported)";
        if (!why.empty()) {
            result.diagnostics.push_back({core::Severity::Warning, "formats.dxf.loop_ignored",
                                          "Closed outline " + describe(ccw[i]) + " (source coordinates) is not used: " +
                                              why + ".",
                                          {}});
            continue;
        }
        accepted.push_back(i);
    }
    const auto box = geometry::bounds(ccw[outer]);
    const Vec2 offset = -box.min;
    result.shape.outer = moved(ccw[outer], offset);
    for (std::size_t i : accepted) result.shape.cutouts.push_back(moved(ccw[i], offset));
    std::stable_sort(result.shape.cutouts.begin(), result.shape.cutouts.end(), [](const Contour& a, const Contour& b) {
        const auto ba = geometry::bounds(a), bb = geometry::bounds(b);
        return ba.min.y != bb.min.y ? ba.min.y < bb.min.y : ba.min.x < bb.min.x;
    });
    for (const auto& c : result.shape.cutouts) result.cutoutKinds.push_back(classify(c));
    const auto b = geometry::bounds(result.shape.outer);
    result.length = snap(b.max.x);
    result.width = snap(b.max.y);
    return result;
}

} // namespace fos::formats
