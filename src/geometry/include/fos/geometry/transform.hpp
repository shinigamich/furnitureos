// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/geometry/vec.hpp"

namespace fos::geometry {

/// Rigid transform (proper rotation + translation) mapping a child frame into its
/// parent frame:  p_parent = R * p_child + t.
///
/// All FurnitureOS frames are right-handed. The columns of R are the child axes
/// expressed in the parent frame. Mirroring is deliberately not representable here:
/// a mirrored part is a different part to manufacture (see ARCHITECTURE.md).
struct Transform {
    Vec3 xAxis{1, 0, 0};
    Vec3 yAxis{0, 1, 0};
    Vec3 zAxis{0, 0, 1};
    Vec3 origin{0, 0, 0};

    static constexpr Transform identity() noexcept { return {}; }
    static constexpr Transform translation(Vec3 t) noexcept { return {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, t}; }
    /// Frame from two orthonormal axes; the third is their cross product (right-handed).
    static constexpr Transform fromAxes(Vec3 x, Vec3 y, Vec3 originPoint) noexcept {
        return {x, y, cross(x, y), originPoint};
    }
    /// Counter-clockwise rotation (degrees) about the parent X / Y / Z axis through the origin.
    static Transform rotationX(double deg) noexcept {
        const double c = std::cos(core::degToRad(deg)), s = std::sin(core::degToRad(deg));
        return {{1, 0, 0}, {0, c, s}, {0, -s, c}, {}};
    }
    static Transform rotationY(double deg) noexcept {
        const double c = std::cos(core::degToRad(deg)), s = std::sin(core::degToRad(deg));
        return {{c, 0, -s}, {0, 1, 0}, {s, 0, c}, {}};
    }
    static Transform rotationZ(double deg) noexcept {
        const double c = std::cos(core::degToRad(deg)), s = std::sin(core::degToRad(deg));
        return {{c, s, 0}, {-s, c, 0}, {0, 0, 1}, {}};
    }

    constexpr Vec3 applyVector(Vec3 v) const noexcept { return xAxis * v.x + yAxis * v.y + zAxis * v.z; }
    constexpr Vec3 applyPoint(Vec3 p) const noexcept { return applyVector(p) + origin; }

    /// (*this) * child : first apply `child`, then this.
    constexpr Transform operator*(const Transform& child) const noexcept {
        return {applyVector(child.xAxis), applyVector(child.yAxis), applyVector(child.zAxis),
                applyPoint(child.origin)};
    }

    constexpr Transform inverse() const noexcept {
        // R^-1 = R^T for a rotation.
        const Vec3 ix{xAxis.x, yAxis.x, zAxis.x};
        const Vec3 iy{xAxis.y, yAxis.y, zAxis.y};
        const Vec3 iz{xAxis.z, yAxis.z, zAxis.z};
        const Vec3 o{-dot(xAxis, origin), -dot(yAxis, origin), -dot(zAxis, origin)};
        return {ix, iy, iz, o};
    }

    /// True when the axes are orthonormal and right-handed within `eps`.
    bool isRigid(double eps = 1e-9) const noexcept {
        return core::approxEqual(dot(xAxis, xAxis), 1, eps) && core::approxEqual(dot(yAxis, yAxis), 1, eps) &&
               core::approxEqual(dot(zAxis, zAxis), 1, eps) && core::approxZero(dot(xAxis, yAxis), eps) &&
               approxEqual(cross(xAxis, yAxis), zAxis, eps);
    }
};

inline bool approxEqual(const Transform& a, const Transform& b, double eps = core::kLengthEps) noexcept {
    return approxEqual(a.xAxis, b.xAxis, eps) && approxEqual(a.yAxis, b.yAxis, eps) &&
           approxEqual(a.zAxis, b.zAxis, eps) && approxEqual(a.origin, b.origin, eps);
}

} // namespace fos::geometry
