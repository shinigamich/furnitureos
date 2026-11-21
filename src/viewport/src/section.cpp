// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/viewport/section.hpp"

#include <algorithm>

namespace fos::viewport {
using geometry::Vec3;

double SectionPlane::coordinate(Vec3 p) const noexcept {
    switch (axis) {
    case SectionAxis::X: return p.x;
    case SectionAxis::Y: return p.y;
    case SectionAxis::Z: break;
    }
    return p.z;
}

double SectionPlane::signedDistance(Vec3 p) const noexcept {
    const double c = coordinate(p);
    return flip ? c - position : position - c;
}

Vec3 SectionPlane::keptNormal() const noexcept {
    const double s = flip ? 1.0 : -1.0;
    switch (axis) {
    case SectionAxis::X: return {s, 0, 0};
    case SectionAxis::Y: return {0, s, 0};
    case SectionAxis::Z: break;
    }
    return {0, 0, s};
}

std::array<double, 4> SectionPlane::equation() const noexcept {
    const Vec3 n = keptNormal();
    // signedDistance = n . p + d; with !flip: -c + position, with flip: c - position.
    return {n.x, n.y, n.z, flip ? -position : position};
}

SectionSide classify(const SectionPlane& plane, const geometry::Aabb& box, double eps) noexcept {
    if (box.empty()) return SectionSide::Removed;
    const double a = plane.signedDistance(box.min), b = plane.signedDistance(box.max);
    const double lo = std::min(a, b), hi = std::max(a, b);
    if (lo >= -eps) return SectionSide::Kept;
    if (hi <= eps) return SectionSide::Removed;
    return SectionSide::Cut;
}

} // namespace fos::viewport
