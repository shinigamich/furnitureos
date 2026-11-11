// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/geometry/offset.hpp"

#include "fos/core/units.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>

namespace fos::geometry {

namespace {

std::string mm(double v) { return core::formatLength(v, 3) + " mm"; }

std::string describe(const Polygon2& p) {
    const Rect2 r = bounds(p);
    return std::to_string(p.size()) + "-vertex polygon with bounding box " + core::formatLength(r.width(), 3) +
           " x " + mm(r.height());
}

core::Error collapsed(const Polygon2& p, double distance) {
    return core::makeError(core::ErrorCode::ConstraintViolation,
                           "Offsetting the " + describe(p) + " by " + mm(distance) +
                               " leaves no area: the polygon collapses.");
}

/// Outward unit normal of edge a -> b of a counter-clockwise ring.
Vec2 outwardNormal(Vec2 a, Vec2 b) noexcept {
    const Vec2 d = b - a;
    const double len = length(d);
    return {d.y / len, -d.x / len};
}

/// Inward offset of a convex CCW polygon: intersection with the shifted half-planes
/// (Sutherland-Hodgman clipping, one half-plane per edge).
core::Result<Polygon2> deflateConvex(const Polygon2& ccw, double distance, double eps) {
    std::vector<Vec2> current = ccw.vertices;
    const std::size_t n = ccw.size();
    for (std::size_t i = 0; i < n && !current.empty(); ++i) {
        const Vec2 a = ccw[i];
        const Vec2 normal = outwardNormal(a, ccw[(i + 1) % n]);
        // Signed distance outside the shifted edge line; keep points with s <= 0.
        const auto side = [&](Vec2 p) { return dot(p - a, normal) - distance; };
        std::vector<Vec2> next;
        next.reserve(current.size() + 1);
        for (std::size_t k = 0; k < current.size(); ++k) {
            const Vec2 p = current[k], q = current[(k + 1) % current.size()];
            const double sp = side(p), sq = side(q);
            if (sp <= 0.0) next.push_back(p);
            if ((sp < 0.0 && sq > 0.0) || (sp > 0.0 && sq < 0.0)) next.push_back(p + (q - p) * (sp / (sp - sq)));
        }
        current = std::move(next);
    }
    Polygon2 result = cleaned({std::move(current)}, eps);
    if (result.size() < 3 || winding(result, eps) != Winding::CounterClockwise) return collapsed(ccw, distance);
    // Clipping rotates the ring arbitrarily: start at the vertex nearest to input vertex 0.
    std::size_t first = 0;
    for (std::size_t i = 1; i < result.size(); ++i)
        if (length(result[i] - ccw[0]) < length(result[first] - ccw[0])) first = i;
    std::rotate(result.vertices.begin(), result.vertices.begin() + static_cast<std::ptrdiff_t>(first),
                result.vertices.end());
    return result;
}

} // namespace

core::Result<Polygon2> offsetPolygon(const Polygon2& polygon, double distance, const OffsetOptions& options) {
    const double eps = options.eps;
    if (!std::isfinite(distance))
        return core::makeError(core::ErrorCode::InvalidArgument, "Polygon offset distance must be a finite number.");
    const Polygon2 ccw = normalizedCcw(cleaned(polygon, eps));
    if (!isSimple(ccw, eps))
        return core::makeError(core::ErrorCode::InvalidArgument,
                               "Cannot offset the " + describe(polygon) +
                                   ": it is not a simple polygon (it self-intersects or has no area).");
    if (distance == 0.0) return ccw;

    const bool convex = isConvex(ccw, eps);
    if (convex && distance < 0.0) return deflateConvex(ccw, distance, eps);

    const std::size_t n = ccw.size();
    const double limit = std::max(1.0, options.miterLimit);
    std::vector<Vec2> out;
    out.reserve(2 * n);
    // firstOf[i] / lastOf[i]: index in `out` of the first / last join point of vertex i.
    std::vector<std::size_t> firstOf(n), lastOf(n);
    for (std::size_t i = 0; i < n; ++i) {
        const Vec2 prev = ccw[(i + n - 1) % n], v = ccw[i], next = ccw[(i + 1) % n];
        const Vec2 n0 = outwardNormal(prev, v), n1 = outwardNormal(v, next);
        const double cosTurn = dot(n0, n1);
        const double turn = cross(v - prev, next - v); // > 0: convex corner of the CCW ring
        const bool joinSide = (distance > 0.0) == (turn > 0.0);
        const double denom = 1.0 + cosTurn;
        const double ratio = denom > 0.0 ? std::sqrt(2.0 / denom) : std::numeric_limits<double>::infinity();
        firstOf[i] = out.size();
        if (joinSide && ratio > limit) {
            // Bevel: cut the corner by the line perpendicular to the corner bisector at
            // exactly |distance| from the vertex; the cut meets the two offset edge lines.
            const Vec2 sum = n0 + n1;
            const double sumLen = length(sum);
            if (sumLen < 1e-12) return collapsed(ccw, distance); // hairpin corner
            const Vec2 bisector = sum * (1.0 / sumLen);
            const double cosHalf = dot(n0, bisector); // == dot(n1, bisector) >= 0
            out.push_back(v + (bisector + (n0 - bisector * cosHalf) * (1.0 / (1.0 + cosHalf))) * distance);
            out.push_back(v + (bisector + (n1 - bisector * cosHalf) * (1.0 / (1.0 + cosHalf))) * distance);
        } else {
            if (denom < 1e-12) return collapsed(ccw, distance); // hairpin corner: offset lines do not meet
            out.push_back(v + (n0 + n1) * (distance / denom)); // intersection of the two offset lines
        }
        lastOf[i] = out.size() - 1;
    }

    // Every original edge must survive with its direction (otherwise it vanished or flipped).
    for (std::size_t i = 0; i < n; ++i) {
        const Vec2 dir = ccw[(i + 1) % n] - ccw[i];
        const Vec2 offsetEdge = out[firstOf[(i + 1) % n]] - out[lastOf[i]];
        if (dot(offsetEdge, dir) / length(dir) <= eps) {
            if (convex) return collapsed(ccw, distance);
            return core::makeError(core::ErrorCode::ConstraintViolation,
                                   "Offsetting the " + describe(ccw) + " by " + mm(distance) + " makes its " +
                                       mm(length(dir)) + " long edge starting at (" +
                                       core::formatLength(ccw[i].x, 3) + ", " + core::formatLength(ccw[i].y, 3) +
                                       ") vanish; offsets that remove edges are only supported for convex polygons.");
        }
    }

    Polygon2 result = cleaned({std::move(out)}, eps);
    // Simple, still counter-clockwise and nested with the input (outside it when inflating,
    // inside it when deflating); anything else means the raw offset outline crossed itself.
    const bool valid = isSimple(result, eps) && winding(result, eps) == Winding::CounterClockwise &&
                       (distance > 0.0 ? polygonContainsPolygon(result, ccw, eps)
                                       : polygonContainsPolygon(ccw, result, eps));
    if (!valid)
        return core::makeError(core::ErrorCode::Unsupported,
                               "Offsetting the " + describe(ccw) + " by " + mm(distance) +
                                   " makes the outline intersect itself; such offsets are not supported.");
    return result;
}

} // namespace fos::geometry
