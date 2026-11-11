// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/core/units.hpp"
#include "fos/geometry/vec.hpp"

#include <algorithm>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

/// Small 2D helpers private to the drawings module (rectangles, segment clipping,
/// hatching). All coordinates are millimetres; no frame is implied here.
namespace fos::drawings {

using geometry::Vec2;
using geometry::Vec3;

/// Axis-aligned 2D rectangle. A default-constructed rectangle is empty.
struct Rect2 {
    Vec2 min{std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity()};
    Vec2 max{-std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()};

    static Rect2 fromCorners(Vec2 a, Vec2 b) noexcept {
        return {{std::min(a.x, b.x), std::min(a.y, b.y)}, {std::max(a.x, b.x), std::max(a.y, b.y)}};
    }
    bool empty() const noexcept { return min.x > max.x || min.y > max.y; }
    double width() const noexcept { return empty() ? 0.0 : max.x - min.x; }
    double height() const noexcept { return empty() ? 0.0 : max.y - min.y; }
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
    /// Closed containment, widened by `eps` (negative eps = strictly inside by |eps|).
    bool contains(Vec2 p, double eps = 0.0) const noexcept {
        return p.x >= min.x - eps && p.x <= max.x + eps && p.y >= min.y - eps && p.y <= max.y + eps;
    }
    Rect2 translated(Vec2 d) const noexcept { return empty() ? *this : Rect2{min + d, max + d}; }
};

/// True when the two rectangles share an area of positive size (overlap deeper than
/// `eps` in both directions). Rectangles that merely touch do not intersect.
bool intersects(const Rect2& a, const Rect2& b, double eps = core::kLengthEps) noexcept;

struct Segment2 {
    Vec2 a, b;
};

/// Liang-Barsky clipping of segment a + t (b - a), t in [0,1], against the CLOSED
/// rectangle. Returns the parameter interval [t0, t1] inside, or nullopt when the
/// segment misses the rectangle. A segment that only touches yields t0 == t1.
std::optional<std::pair<double, double>> clipSegmentParams(const Segment2& s, const Rect2& r) noexcept;

/// The part of `s` inside the closed rectangle, or nullopt when that part is empty
/// or shorter than core::kLengthEps.
std::optional<Segment2> clipSegmentToRect(const Segment2& s, const Rect2& r) noexcept;

/// True when the segment passes through the open interior of `r` (deeper than eps).
bool segmentCrossesRect(const Segment2& s, const Rect2& r, double eps = core::kLengthEps) noexcept;

/// Parallel hatch lines at `angleDeg` (counter-clockwise from +x) clipped to `r`.
/// The family is anchored at the coordinate origin: line k satisfies n . p = k * spacing
/// with n = (-sin a, cos a), so hatches of neighbouring rectangles line up.
/// Lines that would only touch a corner are omitted. Returns an empty list when
/// spacing <= 0, the rectangle is empty, or more than 20000 lines would be needed.
std::vector<Segment2> hatchRect(const Rect2& r, double angleDeg, double spacing);

} // namespace fos::drawings
