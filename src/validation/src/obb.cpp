// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/validation/obb.hpp"

#include <cmath>
#include <limits>

namespace fos::validation {
using geometry::Vec3;

Obb makeObb(const geometry::Aabb& localBox, const geometry::Transform& toFrame) noexcept {
    Obb o;
    o.center = toFrame.applyPoint(localBox.center());
    o.axes = {toFrame.xAxis, toFrame.yAxis, toFrame.zAxis};
    o.half = localBox.size() * 0.5;
    return o;
}

namespace {
double radius(const Obb& o, Vec3 n) noexcept {
    return o.half.x * std::fabs(dot(o.axes[0], n)) + o.half.y * std::fabs(dot(o.axes[1], n)) +
           o.half.z * std::fabs(dot(o.axes[2], n));
}
} // namespace

double projectedExtent(const Obb& box, Vec3 dir) noexcept { return 2.0 * radius(box, dir); }

ObbOverlap intersectObb(const Obb& a, const Obb& b, double tolerance) noexcept {
    const Vec3 t = b.center - a.center;
    ObbOverlap best;
    best.depth = std::numeric_limits<double>::infinity();

    // Returns false when `n` (unit) separates the boxes. `bias` > 0 makes an axis lose ties.
    auto test = [&](Vec3 n, double bias) {
        const double dist = dot(t, n);
        const double overlap = radius(a, n) + radius(b, n) - std::fabs(dist);
        if (overlap < best.depth - bias) {
            best.depth = overlap;
            best.axis = dist < 0 ? -n : n;
        }
        if (overlap > tolerance) return true;
        best.depth = overlap;
        best.axis = dist < 0 ? -n : n;
        return false;
    };

    for (const Vec3& n : a.axes)
        if (!test(n, 0.0)) return best;
    for (const Vec3& n : b.axes)
        if (!test(n, 0.0)) return best;
    for (const Vec3& ea : a.axes) {
        for (const Vec3& eb : b.axes) {
            const Vec3 c = cross(ea, eb);
            const double len = length(c);
            if (len < 1e-8) continue; // parallel edges: axis covered by the face normals
            if (!test(c * (1.0 / len), 1e-9)) return best;
        }
    }
    best.intersects = true;
    return best;
}

} // namespace fos::validation
