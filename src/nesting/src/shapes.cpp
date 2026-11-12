// SPDX-License-Identifier: AGPL-3.0-only
#include "shapes.hpp"

#include "fos/geometry/triangulate.hpp"

#include <algorithm>
#include <cmath>

namespace fos::nesting::detail {

Polygon2 transformOutline(const Polygon2& outline, double rotationDeg, bool mirrored, Vec2 translation) {
    const Polygon2 m = mirrored ? geometry::mirroredX(outline, 0.0) : outline;
    return geometry::translated(geometry::rotated(m, rotationDeg), translation);
}

namespace {

bool isConvexRing(const std::vector<Vec2>& pts) {
    const std::size_t n = pts.size();
    if (n < 3) return false;
    for (std::size_t i = 0; i < n; ++i) {
        const Vec2 a = pts[i], b = pts[(i + 1) % n], c = pts[(i + 2) % n];
        const Vec2 u = b - a, v = c - b;
        const double lu = geometry::length(u), lv = geometry::length(v);
        if (lu == 0 || lv == 0) return false;
        // Allow straight (collinear) joints; reject right turns.
        if (geometry::cross(u, v) < -1e-12 * lu * lv) return false;
    }
    return true;
}

/// True when the two rings have the same vertex sequence up to a cyclic shift (within 1e-9 mm).
bool sameRing(const Polygon2& a, const Polygon2& b) {
    const std::size_t n = a.size();
    if (n != b.size() || n == 0) return false;
    for (std::size_t s = 0; s < n; ++s) {
        bool ok = true;
        for (std::size_t i = 0; i < n && ok; ++i)
            ok = geometry::approxEqual(a[i], b[(i + s) % n], 1e-9);
        if (ok) return true;
    }
    return false;
}

} // namespace

std::vector<Polygon2> convexPieces(const Polygon2& ccw) {
    if (geometry::isConvex(ccw)) return {ccw};
    auto tri = geometry::triangulate(ccw);
    if (!tri) return {geometry::convexHull(ccw.vertices)}; // unreachable for valid input
    std::vector<std::vector<std::uint32_t>> pieces;
    for (const auto& t : tri.value()) pieces.push_back({t[0], t[1], t[2]});
    const auto ring = [&](const std::vector<std::uint32_t>& idx) {
        std::vector<Vec2> pts;
        for (auto i : idx) pts.push_back(ccw.vertices[i]);
        return pts;
    };
    bool merged = true;
    while (merged) {
        merged = false;
        for (std::size_t a = 0; a < pieces.size() && !merged; ++a)
            for (std::size_t b = a + 1; b < pieces.size() && !merged; ++b) {
                const auto& A = pieces[a];
                const auto& B = pieces[b];
                // Find edge i->j in A whose reverse j->i is in B.
                for (std::size_t ia = 0; ia < A.size() && !merged; ++ia) {
                    const auto i = A[ia], j = A[(ia + 1) % A.size()];
                    for (std::size_t ib = 0; ib < B.size(); ++ib) {
                        if (B[ib] != j || B[(ib + 1) % B.size()] != i) continue;
                        // A from j around to i, then B from i around to j (exclusive ends).
                        std::vector<std::uint32_t> u;
                        for (std::size_t k = 0; k < A.size(); ++k) u.push_back(A[(ia + 1 + k) % A.size()]);
                        for (std::size_t k = 2; k < B.size(); ++k) u.push_back(B[(ib + k) % B.size()]);
                        if (isConvexRing(ring(u))) {
                            pieces[a] = u;
                            pieces.erase(pieces.begin() + static_cast<std::ptrdiff_t>(b));
                            merged = true;
                        }
                        break;
                    }
                }
            }
    }
    std::vector<Polygon2> out;
    for (const auto& p : pieces) out.push_back(geometry::cleaned(Polygon2{ring(p)}));
    return out;
}

Polygon2 minkowskiSumConvex(const Polygon2& a, const Polygon2& b) {
    std::vector<Vec2> sums;
    sums.reserve(a.size() * b.size());
    for (auto p : a.vertices)
        for (auto q : b.vertices) sums.push_back(p + q);
    return geometry::convexHull(sums);
}

Shape buildShape(const Part& part, const std::vector<double>& rotations) {
    Shape s;
    s.area = geometry::area(part.outline);
    s.netArea = s.area;
    for (const auto& h : part.holes) s.netArea -= geometry::area(h);
    std::vector<Polygon2> keys;
    for (int m = 0; m < (part.mirrorAllowed ? 2 : 1); ++m)
        for (double rot : rotations) {
            Orientation o;
            o.rotationDeg = rot;
            o.mirrored = m == 1;
            o.transformed = transformOutline(part.outline, rot, o.mirrored, {});
            const Rect2 bb = geometry::bounds(o.transformed);
            o.offset = bb.min;
            o.w = bb.width();
            o.h = bb.height();
            const Polygon2 norm = geometry::translated(o.transformed, -o.offset);
            Polygon2 key = geometry::normalizedCcw(geometry::cleaned(norm));
            bool dup = false;
            for (const auto& k : keys) dup = dup || sameRing(k, key);
            if (dup) continue;
            o.pieces = convexPieces(key);
            keys.push_back(std::move(key));
            s.orients.push_back(std::move(o));
        }
    return s;
}

} // namespace fos::nesting::detail
