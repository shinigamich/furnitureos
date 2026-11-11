// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/geometry/triangulate.hpp"

#include <string>

namespace fos::geometry {

namespace {

/// True when `p` is inside or on the boundary (within eps) of the CCW triangle a, b, c.
bool inClosedTriangle(Vec2 a, Vec2 b, Vec2 c, Vec2 p, double eps) noexcept {
    return orientation(a, b, p, eps) != Orientation::Clockwise &&
           orientation(b, c, p, eps) != Orientation::Clockwise &&
           orientation(c, a, p, eps) != Orientation::Clockwise;
}

} // namespace

core::Result<std::vector<TriangleIndices>> triangulate(const Polygon2& polygon, double eps) {
    const std::size_t n = polygon.size();
    if (!isSimple(polygon, eps))
        return core::makeError(core::ErrorCode::InvalidArgument,
                               "Cannot triangulate the polygon with " + std::to_string(n) +
                                   " vertices: it is not simple (fewer than 3 vertices, repeated vertices, "
                                   "self-intersection or zero area). Use cleaned() to remove repeated vertices.");

    // Remaining ring as indices into the polygon, always traversed counter-clockwise.
    std::vector<std::uint32_t> ring(n);
    const bool ccw = signedArea(polygon) > 0.0;
    for (std::size_t i = 0; i < n; ++i) ring[i] = static_cast<std::uint32_t>(ccw ? i : n - 1 - i);

    std::vector<TriangleIndices> triangles;
    triangles.reserve(n - 2);
    while (ring.size() > 3) {
        const std::size_t m = ring.size();
        bool clipped = false;
        for (std::size_t i = 0; i < m && !clipped; ++i) {
            const std::uint32_t ip = ring[(i + m - 1) % m], ic = ring[i], in = ring[(i + 1) % m];
            const Vec2 a = polygon[ip], b = polygon[ic], c = polygon[in];
            if (orientation(a, b, c, eps) != Orientation::CounterClockwise) continue; // reflex or flat corner
            bool empty = true;
            for (std::size_t k = 0; k < m && empty; ++k) {
                const std::uint32_t idx = ring[k];
                if (idx == ip || idx == ic || idx == in) continue;
                empty = !inClosedTriangle(a, b, c, polygon[idx], eps);
            }
            if (!empty) continue;
            triangles.push_back({ip, ic, in});
            ring.erase(ring.begin() + static_cast<std::ptrdiff_t>(i));
            clipped = true;
        }
        if (!clipped)
            return core::makeError(core::ErrorCode::Internal,
                                   "Triangulation found no ear with " + std::to_string(ring.size()) +
                                       " vertices remaining; the polygon is numerically degenerate at tolerance " +
                                       std::to_string(eps) + " mm.");
    }
    if (orientation(polygon[ring[0]], polygon[ring[1]], polygon[ring[2]], eps) != Orientation::CounterClockwise)
        return core::makeError(core::ErrorCode::Internal,
                               "Triangulation ended with a degenerate triangle; the polygon is numerically "
                               "degenerate at tolerance " + std::to_string(eps) + " mm.");
    triangles.push_back({ring[0], ring[1], ring[2]});
    return triangles;
}

} // namespace fos::geometry
