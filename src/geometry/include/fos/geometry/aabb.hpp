// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/geometry/transform.hpp"

#include <algorithm>
#include <limits>

namespace fos::geometry {

/// Axis-aligned bounding box. Default-constructed box is empty.
struct Aabb {
    Vec3 min{std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity(),
             std::numeric_limits<double>::infinity()};
    Vec3 max{-std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity(),
             -std::numeric_limits<double>::infinity()};

    static Aabb fromSize(Vec3 size) noexcept { return {{0, 0, 0}, size}; }

    bool empty() const noexcept { return min.x > max.x || min.y > max.y || min.z > max.z; }
    Vec3 size() const noexcept { return empty() ? Vec3{} : max - min; }
    Vec3 center() const noexcept { return (min + max) * 0.5; }

    void expand(Vec3 p) noexcept {
        min = {std::min(min.x, p.x), std::min(min.y, p.y), std::min(min.z, p.z)};
        max = {std::max(max.x, p.x), std::max(max.y, p.y), std::max(max.z, p.z)};
    }
    void expand(const Aabb& o) noexcept {
        if (o.empty()) return;
        expand(o.min);
        expand(o.max);
    }

    /// Bounding box of this box after transformation (encloses all 8 corners).
    Aabb transformed(const Transform& t) const noexcept {
        Aabb out;
        if (empty()) return out;
        for (int i = 0; i < 8; ++i)
            out.expand(t.applyPoint({(i & 1) ? max.x : min.x, (i & 2) ? max.y : min.y, (i & 4) ? max.z : min.z}));
        return out;
    }

    /// Volume overlap test. Boxes that merely touch (shared face/edge within eps) do not overlap.
    bool overlaps(const Aabb& o, double eps = core::kLengthEps) const noexcept {
        if (empty() || o.empty()) return false;
        return min.x < o.max.x - eps && o.min.x < max.x - eps && min.y < o.max.y - eps &&
               o.min.y < max.y - eps && min.z < o.max.z - eps && o.min.z < max.z - eps;
    }
};

} // namespace fos::geometry
