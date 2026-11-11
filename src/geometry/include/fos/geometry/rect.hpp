// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/geometry/vec.hpp"

#include <algorithm>
#include <limits>

namespace fos::geometry {

/// Axis-aligned 2D rectangle / bounding box (mm). Default-constructed rectangle is empty.
struct Rect2 {
    Vec2 min{std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity()};
    Vec2 max{-std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()};

    /// Rectangle [0,width] x [0,height].
    static constexpr Rect2 fromSize(double width, double height) noexcept { return {{0, 0}, {width, height}}; }
    /// Rectangle with lower-left corner `origin` and the given size.
    static constexpr Rect2 fromOriginSize(Vec2 origin, double width, double height) noexcept {
        return {origin, {origin.x + width, origin.y + height}};
    }
    /// Smallest rectangle containing both points (any two opposite corners).
    static Rect2 fromCorners(Vec2 p, Vec2 q) noexcept {
        return {{std::min(p.x, q.x), std::min(p.y, q.y)}, {std::max(p.x, q.x), std::max(p.y, q.y)}};
    }

    bool empty() const noexcept { return min.x > max.x || min.y > max.y; }
    double width() const noexcept { return empty() ? 0.0 : max.x - min.x; }
    double height() const noexcept { return empty() ? 0.0 : max.y - min.y; }
    double area() const noexcept { return width() * height(); }
    Vec2 center() const noexcept { return (min + max) * 0.5; }

    void expand(Vec2 p) noexcept {
        min = {std::min(min.x, p.x), std::min(min.y, p.y)};
        max = {std::max(max.x, p.x), std::max(max.y, p.y)};
    }
    void expand(const Rect2& o) noexcept {
        if (o.empty()) return;
        expand(o.min);
        expand(o.max);
    }
    /// Rectangle grown by `d` on every side (shrunk for negative d; may become empty).
    Rect2 inflated(double d) const noexcept { return {{min.x - d, min.y - d}, {max.x + d, max.y + d}}; }

    /// True when `p` is inside or within `eps` of the rectangle.
    bool contains(Vec2 p, double eps = core::kLengthEps) const noexcept {
        return p.x >= min.x - eps && p.x <= max.x + eps && p.y >= min.y - eps && p.y <= max.y + eps;
    }
    bool operator==(const Rect2&) const noexcept = default;
};

/// True when the rectangles are closer than `gap` to each other, measured per axis
/// (Chebyshev / saw-kerf convention): they "overlap" when BOTH the X and the Y separation
/// are smaller than `gap - eps`. With gap = 0 this is an interior-overlap test: rectangles
/// that merely share an edge or a corner (within eps) do NOT overlap. With gap = g,
/// rectangles exactly g apart are accepted (no overlap), rectangles closer than g overlap.
/// Empty rectangles never overlap.
inline bool rectsOverlap(const Rect2& a, const Rect2& b, double gap = 0.0,
                         double eps = core::kLengthEps) noexcept {
    if (a.empty() || b.empty()) return false;
    const double g = gap - eps;
    return a.min.x < b.max.x + g && b.min.x < a.max.x + g && a.min.y < b.max.y + g && b.min.y < a.max.y + g;
}

/// True when `inner` lies inside `outer` keeping at least `margin` to every side of `outer`
/// (within eps). With margin = 0, an inner rectangle touching the outer boundary is contained.
inline bool rectContainsRect(const Rect2& outer, const Rect2& inner, double margin = 0.0,
                             double eps = core::kLengthEps) noexcept {
    if (outer.empty() || inner.empty()) return false;
    const double m = margin - eps;
    return inner.min.x >= outer.min.x + m && inner.min.y >= outer.min.y + m && inner.max.x <= outer.max.x - m &&
           inner.max.y <= outer.max.y - m;
}

} // namespace fos::geometry
