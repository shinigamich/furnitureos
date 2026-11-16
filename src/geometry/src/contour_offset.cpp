// SPDX-License-Identifier: AGPL-3.0-only
// Per-segment offset of line/arc contours (ADR-0008): lines move along their normal, arcs stay
// concentric, neighbours are re-joined exactly. See src/geometry/README.md for the rules.
#include "contour_internal.hpp"
#include "fos/geometry/contour.hpp"
#include "fos/geometry/curve_intersect.hpp"

#include <cmath>
#include <limits>
#include <string>
#include <vector>

namespace fos::geometry {
namespace {

using core::formatLength;

/// Junctions whose tangents differ by less than this (|sin| of the turning angle) are treated
/// as tangent (G1) junctions.
constexpr double kTangentSin = 1e-6;

core::Error offsetError(std::string message) {
    return core::makeError(core::ErrorCode::ConstraintViolation, std::move(message), "offsetContour");
}

/// An offset primitive: the infinite line or full circle carrying an offset segment.
struct Primitive {
    bool isArc = false;
    Vec2 point, dir;  ///< Line: a point and the unit direction of travel.
    Vec2 center;      ///< Arc: centre (unchanged by the offset) and new radius.
    double radius = 0;
};

std::vector<Vec2> intersect(const Primitive& a, const Primitive& b) {
    if (!a.isArc && !b.isArc) {
        if (auto p = intersectLines(a.point, a.dir, b.point, b.dir)) return {*p};
        return {};
    }
    if (a.isArc && b.isArc) return intersectCircles(a.center, a.radius, b.center, b.radius, 0.0);
    const Primitive& line = a.isArc ? b : a;
    const Primitive& arc = a.isArc ? a : b;
    return intersectLineCircle(line.point, line.dir, arc.center, arc.radius, 0.0);
}

/// How the offset segments meet at one original vertex.
struct Junction {
    Vec2 endPrev;    ///< New end of the incoming segment.
    Vec2 startNext;  ///< New start of the outgoing segment.
    bool connector = false; ///< A connecting segment endPrev -> startNext is inserted.
    double connectorBulge = 0.0;
};

} // namespace

core::Result<Contour> offsetContour(const Contour& c, const std::vector<double>& distance, double eps) {
    const std::size_t n = c.size();
    if (distance.size() != n)
        return core::makeError(core::ErrorCode::InvalidArgument,
                               "offsetContour needs one distance per segment: got " + std::to_string(distance.size()) +
                                   " distances for " + std::to_string(n) + " segments.",
                               "offsetContour");
    for (std::size_t i = 0; i < n; ++i)
        if (!std::isfinite(distance[i]))
            return core::makeError(core::ErrorCode::InvalidArgument,
                                   "Offset distance of segment " + std::to_string(i) + " is not finite.",
                                   "offsetContour");
    if (auto v = validateContour(c, eps); !v) return v.error();

    // o = +1 for a counter-clockwise contour, -1 for a clockwise one. The outward normal of a
    // segment with unit tangent t is o * rightNormal(t).
    const double area = signedArea(c);
    const double o = area > 0 ? 1.0 : -1.0;

    std::vector<ContourSegmentInfo> seg(n);
    std::vector<Primitive> prim(n);
    for (std::size_t i = 0; i < n; ++i) {
        seg[i] = segmentInfo(c, i);
        const ContourSegmentInfo& s = seg[i];
        Primitive& p = prim[i];
        if (!s.isArc) {
            p.dir = detail::tangentAtStart(s);
            p.point = s.start + detail::rightNormal(p.dir) * (o * distance[i]);
            continue;
        }
        // Arc radius rule: a counter-clockwise arc (sweep > 0) has its centre on its LEFT; on a
        // CCW contour (o = +1) the left is the inside, so an outward offset d moves away from the
        // centre: r' = r + d. A clockwise arc (concave on a CCW contour) has its centre outside:
        // r' = r - d. In general r' = r + d * o * sign(sweep).
        const double sign = s.sweep > 0 ? 1.0 : -1.0;
        p.isArc = true;
        p.center = s.center;
        p.radius = s.radius + distance[i] * o * sign;
        if (p.radius <= eps)
            return offsetError("Arc segment " + std::to_string(i) + " (radius " + formatLength(s.radius, 3) +
                               " mm) would get radius " + formatLength(p.radius, 3) + " mm after an offset of " +
                               formatLength(distance[i], 3) + " mm; the radius must stay positive.");
    }

    std::vector<Junction> junction(n);
    for (std::size_t i = 0; i < n; ++i) {
        const std::size_t prev = (i + n - 1) % n;
        const Vec2 v = c.vertices[i].point;
        const Vec2 t1 = detail::tangentAtEnd(seg[prev]);
        const Vec2 t2 = detail::tangentAtStart(seg[i]);
        const double d1 = distance[prev], d2 = distance[i];
        const Vec2 e1 = v + detail::rightNormal(t1) * (o * d1); // natural end of offset prev
        const Vec2 s2 = v + detail::rightNormal(t2) * (o * d2); // natural start of offset next
        const double sinA = cross(t1, t2), cosA = dot(t1, t2);
        Junction& j = junction[i];
        if (cosA > 0 && std::fabs(sinA) <= kTangentSin) {
            if (std::fabs(d1 - d2) <= eps) {
                // Tangent continuation offset by the same distance: still tangent, shared point.
                j.endPrev = j.startNext = (e1 + s2) * 0.5;
            } else {
                // Tangent junction, different distances: each segment keeps its own offset over
                // its own extent; a straight step along the common normal joins them.
                j.endPrev = e1;
                j.startNext = s2;
                j.connector = true;
            }
            continue;
        }
        const std::vector<Vec2> cand = intersect(prim[prev], prim[i]);
        if (!cand.empty()) {
            Vec2 best = cand.front();
            for (Vec2 p : cand)
                if (length(p - v) < length(best - v)) best = p;
            j.endPrev = j.startNext = best;
            continue;
        }
        if (std::fabs(d1 - d2) <= eps && d1 * o * sinA > 0) {
            // Gap side of the corner (outward at a convex corner, inward at a concave one) and no
            // intersection: round join, an arc of radius |d| about the original vertex.
            j.endPrev = e1;
            j.startNext = s2;
            j.connector = true;
            j.connectorBulge = std::tan(std::atan2(sinA, cosA) / 4.0);
            continue;
        }
        return offsetError("After the offset, segments " + std::to_string(prev) + " and " + std::to_string(i) +
                           " (meeting at " + detail::formatPoint(v) + ") no longer meet (offsets " +
                           formatLength(d1, 3) + " and " + formatLength(d2, 3) + " mm).");
    }

    Contour out;
    for (std::size_t i = 0; i < n; ++i) {
        const ContourSegmentInfo& s = seg[i];
        const Vec2 a = junction[i].startNext;
        const Vec2 b = junction[(i + 1) % n].endPrev;
        double bulge = 0.0;
        const std::string vanish = "Segment " + std::to_string(i) + " (" + detail::formatPoint(s.start) + " -> " +
                                   detail::formatPoint(s.end) + ") would vanish or reverse after the offset.";
        if (!s.isArc) {
            if (length(b - a) <= eps || dot(b - a, s.end - s.start) <= 0) return offsetError(vanish);
        } else {
            const Primitive& p = prim[i];
            const double th = detail::sweepRad(s);
            const double a0 = detail::startRad(s);
            const double ds = detail::wrapPi(detail::angleOf(a - p.center) - a0);
            const double de = detail::wrapPi(detail::angleOf(b - p.center) - (a0 + th));
            const double sw = th + de - ds;
            if (sw * th <= 0 || std::fabs(sw) * p.radius <= eps || length(b - a) <= eps ||
                std::fabs(sw) >= detail::kTwoPi - 1e-9)
                return offsetError(vanish);
            bulge = std::tan(sw / 4.0);
        }
        out.vertices.push_back({a, bulge});
        const Junction& next = junction[(i + 1) % n];
        if (next.connector) {
            if (length(next.startNext - next.endPrev) <= eps) continue; // degenerate step
            out.vertices.push_back({next.endPrev, next.connectorBulge});
        }
    }

    if (auto v = validateContour(out, eps); !v)
        return offsetError("The offset contour is invalid (" + v.error().message + ")");
    if (signedArea(out) * o <= 0)
        return offsetError("The offset contour reverses its orientation; the offset is too large for this shape.");
    return out;
}

core::Result<Contour> offsetContour(const Contour& c, double distance, double eps) {
    return offsetContour(c, std::vector<double>(c.size(), distance), eps);
}

} // namespace fos::geometry
