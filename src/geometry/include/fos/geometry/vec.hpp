// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/core/units.hpp"

#include <cmath>

namespace fos::geometry {

struct Vec2 {
    double x = 0, y = 0;
    constexpr Vec2 operator+(Vec2 o) const noexcept { return {x + o.x, y + o.y}; }
    constexpr Vec2 operator-(Vec2 o) const noexcept { return {x - o.x, y - o.y}; }
    constexpr Vec2 operator*(double s) const noexcept { return {x * s, y * s}; }
    constexpr Vec2 operator-() const noexcept { return {-x, -y}; }
    constexpr bool operator==(const Vec2&) const noexcept = default;
};
constexpr double dot(Vec2 a, Vec2 b) noexcept { return a.x * b.x + a.y * b.y; }
/// z component of the 3D cross product; > 0 when b is counter-clockwise from a.
constexpr double cross(Vec2 a, Vec2 b) noexcept { return a.x * b.y - a.y * b.x; }
inline double length(Vec2 a) noexcept { return std::hypot(a.x, a.y); }
inline bool approxEqual(Vec2 a, Vec2 b, double eps = core::kLengthEps) noexcept {
    return core::approxEqual(a.x, b.x, eps) && core::approxEqual(a.y, b.y, eps);
}

struct Vec3 {
    double x = 0, y = 0, z = 0;
    constexpr Vec3 operator+(Vec3 o) const noexcept { return {x + o.x, y + o.y, z + o.z}; }
    constexpr Vec3 operator-(Vec3 o) const noexcept { return {x - o.x, y - o.y, z - o.z}; }
    constexpr Vec3 operator*(double s) const noexcept { return {x * s, y * s, z * s}; }
    constexpr Vec3 operator-() const noexcept { return {-x, -y, -z}; }
    constexpr bool operator==(const Vec3&) const noexcept = default;
};
constexpr double dot(Vec3 a, Vec3 b) noexcept { return a.x * b.x + a.y * b.y + a.z * b.z; }
constexpr Vec3 cross(Vec3 a, Vec3 b) noexcept {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline double length(Vec3 a) noexcept { return std::sqrt(dot(a, a)); }
inline Vec3 normalized(Vec3 a) noexcept {
    const double l = length(a);
    return l > 0 ? a * (1.0 / l) : a;
}
inline bool approxEqual(Vec3 a, Vec3 b, double eps = core::kLengthEps) noexcept {
    return core::approxEqual(a.x, b.x, eps) && core::approxEqual(a.y, b.y, eps) &&
           core::approxEqual(a.z, b.z, eps);
}

} // namespace fos::geometry
