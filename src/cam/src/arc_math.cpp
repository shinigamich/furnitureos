// SPDX-License-Identifier: AGPL-3.0-only
#include "arc_math.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace fos::cam::detail {

using geometry::Vec2;
namespace {
constexpr double kTwoPi = 2 * std::numbers::pi;
/// Angle normalised into [0, 2 pi).
double wrap(double a) noexcept {
    a = std::fmod(a, kTwoPi);
    if (a < 0) a += kTwoPi;
    return a >= kTwoPi ? 0.0 : a;
}
} // namespace

ArcGeom arcFromEndpoints(Vec2 start, Vec2 end, Vec2 centre, bool ccw) noexcept {
    ArcGeom a;
    a.centre = centre;
    a.radius = geometry::length(start - centre);
    a.startAngle = std::atan2(start.y - centre.y, start.x - centre.x);
    const double endAngle = std::atan2(end.y - centre.y, end.x - centre.x);
    double s = wrap(ccw ? endAngle - a.startAngle : a.startAngle - endAngle);
    if (s < 1e-12) s = kTwoPi;
    a.sweep = ccw ? s : -s;
    return a;
}

Vec2 arcPoint(const ArcGeom& a, double s) noexcept {
    const double t = a.startAngle + a.sweep * s;
    return {a.centre.x + a.radius * std::cos(t), a.centre.y + a.radius * std::sin(t)};
}

double arcLength(const ArcGeom& a) noexcept { return a.radius * std::fabs(a.sweep); }

/// True when angle t (radians) lies on the swept range of the arc.
static bool onSweep(const ArcGeom& a, double t) noexcept {
    const double d = wrap(a.sweep > 0 ? t - a.startAngle : a.startAngle - t);
    return d <= std::fabs(a.sweep) + 1e-12;
}

void arcBounds(const ArcGeom& a, double& x0, double& y0, double& x1, double& y1) noexcept {
    const Vec2 p = arcPoint(a, 0), q = arcPoint(a, 1);
    x0 = std::min(p.x, q.x);
    x1 = std::max(p.x, q.x);
    y0 = std::min(p.y, q.y);
    y1 = std::max(p.y, q.y);
    const double half = std::numbers::pi / 2;
    for (int k = 0; k < 4; ++k) {
        const double t = k * half;
        if (!onSweep(a, t)) continue;
        const Vec2 e{a.centre.x + a.radius * std::cos(t), a.centre.y + a.radius * std::sin(t)};
        x0 = std::min(x0, e.x);
        x1 = std::max(x1, e.x);
        y0 = std::min(y0, e.y);
        y1 = std::max(y1, e.y);
    }
}

double distanceToSegment(Vec2 p, Vec2 a, Vec2 b) noexcept {
    const Vec2 d = b - a;
    const double dd = geometry::dot(d, d);
    const double t = dd < 1e-18 ? 0.0 : std::clamp(geometry::dot(p - a, d) / dd, 0.0, 1.0);
    return geometry::length(p - (a + d * t));
}

double distanceToArc(Vec2 p, const ArcGeom& a) noexcept {
    const Vec2 v = p - a.centre;
    const double d = geometry::length(v);
    double best = std::min(geometry::length(p - arcPoint(a, 0)), geometry::length(p - arcPoint(a, 1)));
    if (d > 1e-12 && onSweep(a, std::atan2(v.y, v.x))) best = std::min(best, std::fabs(d - a.radius));
    return best;
}

ArcGeom segmentArc(const geometry::Contour& c, std::size_t i) noexcept {
    const geometry::ContourSegmentInfo s = geometry::segmentInfo(c, i);
    ArcGeom a;
    a.centre = s.center;
    a.radius = s.radius;
    a.startAngle = std::atan2(s.start.y - s.center.y, s.start.x - s.center.x);
    a.sweep = s.sweep * std::numbers::pi / 180.0;
    return a;
}

double distanceToContour(Vec2 p, const geometry::Contour& c) noexcept {
    double best = 1e300;
    for (std::size_t i = 0; i < c.size(); ++i) {
        const geometry::ContourSegmentInfo s = geometry::segmentInfo(c, i);
        best = std::min(best, s.isArc ? distanceToArc(p, segmentArc(c, i)) : distanceToSegment(p, s.start, s.end));
    }
    return best;
}

} // namespace fos::cam::detail
