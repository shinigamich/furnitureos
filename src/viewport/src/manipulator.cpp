// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/viewport/manipulator.hpp"

#include <algorithm>
#include <cmath>

namespace fos::viewport {

namespace {
double& component(geometry::Vec3& v, HandleAxis a) noexcept {
    return a == HandleAxis::X ? v.x : (a == HandleAxis::Y ? v.y : v.z);
}
double component(const geometry::Vec3& v, HandleAxis a) noexcept {
    return a == HandleAxis::X ? v.x : (a == HandleAxis::Y ? v.y : v.z);
}
geometry::Vec3 frameAxis(const geometry::Transform& t, HandleAxis a) noexcept {
    return a == HandleAxis::X ? t.xAxis : (a == HandleAxis::Y ? t.yAxis : t.zAxis);
}
} // namespace

geometry::Vec3 handleAnchor(const ResizeTarget& target, std::size_t i) noexcept {
    const auto& h = target.handles[i];
    geometry::Vec3 p = target.box.center();
    component(p, h.axis) = component(h.negative ? target.box.min : target.box.max, h.axis);
    return target.frame.applyPoint(p);
}

geometry::Vec3 handleDirection(const ResizeTarget& target, std::size_t i) noexcept {
    const auto& h = target.handles[i];
    const geometry::Vec3 a = geometry::normalized(frameAxis(target.frame, h.axis));
    return h.negative ? a * -1.0 : a;
}

std::optional<double> closestAxisParameter(const geometry::Ray3& ray, geometry::Vec3 origin,
                                           geometry::Vec3 direction) noexcept {
    const geometry::Vec3 d = ray.direction, u = direction, w = ray.origin - origin;
    const double a = geometry::dot(d, d), b = geometry::dot(d, u), c = geometry::dot(u, u);
    const double p = geometry::dot(d, w), q = geometry::dot(u, w);
    const double denom = a * c - b * b; // = a c sin^2(angle)
    if (!(a > 0 && c > 0) || denom < 1e-6 * a * c) return std::nullopt;
    return (a * q - b * p) / denom;
}

double snapToStep(double value, double step) noexcept {
    if (!(step > 0) || !std::isfinite(step)) return value;
    return std::round(value / step) * step;
}

double dragValue(double startValue, double s0, double s, double step, double minimum) noexcept {
    return std::max(snapToStep(startValue + (s - s0), step), minimum);
}

geometry::Aabb ghostBox(const ResizeTarget& target, std::size_t i, double newValue) noexcept {
    const auto& h = target.handles[i];
    geometry::Aabb b = target.box;
    if (h.negative)
        component(b.min, h.axis) = component(b.max, h.axis) - newValue;
    else
        component(b.max, h.axis) = component(b.min, h.axis) + newValue;
    return b;
}

std::optional<std::size_t> handleAtPixel(const ResizeTarget& target, const Camera& camera, double px, double py,
                                         double radiusPx) noexcept {
    std::optional<std::size_t> best;
    double bestDist = radiusPx;
    for (std::size_t i = 0; i < target.handles.size(); ++i) {
        const auto pixel = camera.projectToPixel(handleAnchor(target, i));
        if (!pixel) continue;
        const double dist = std::hypot(pixel->x - px, pixel->y - py);
        if (dist <= bestDist && (!best || dist < bestDist)) {
            best = i;
            bestDist = dist;
        }
    }
    return best;
}

} // namespace fos::viewport
