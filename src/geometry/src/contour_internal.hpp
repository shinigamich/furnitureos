// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/core/units.hpp"
#include "fos/geometry/contour.hpp"

#include <cmath>
#include <string>

/// Internal helpers shared by the contour implementation files (not installed / not public).
namespace fos::geometry::detail {

inline constexpr double kTwoPi = 2.0 * core::kPi;

/// Angle normalised into [0, 2*pi).
inline double normalizeAngle(double a) noexcept {
    double r = std::fmod(a, kTwoPi);
    if (r < 0) r += kTwoPi;
    if (r >= kTwoPi) r = 0.0;
    return r;
}

/// Angle wrapped into (-pi, pi].
inline double wrapPi(double a) noexcept {
    double r = normalizeAngle(a);
    if (r > core::kPi) r -= kTwoPi;
    return r;
}

inline double angleOf(Vec2 v) noexcept { return std::atan2(v.y, v.x); }

/// True when the direction `a` (radians) lies on the arc that starts at `start` and sweeps the
/// signed angle `sweep` (radians), end directions included.
inline bool angleInSweep(double a, double start, double sweep) noexcept {
    const double t = sweep >= 0 ? normalizeAngle(a - start) : normalizeAngle(start - a);
    return t <= std::fabs(sweep);
}

inline Vec2 pointOnCircle(Vec2 c, double r, double a) noexcept { return {c.x + r * std::cos(a), c.y + r * std::sin(a)}; }

inline double startRad(const ContourSegmentInfo& s) noexcept { return core::degToRad(s.startAngle); }
inline double sweepRad(const ContourSegmentInfo& s) noexcept { return core::degToRad(s.sweep); }

/// Unit tangent (direction of travel) at the start / end of a segment.
inline Vec2 tangentAt(const ContourSegmentInfo& s, Vec2 p) noexcept {
    if (!s.isArc) {
        const Vec2 d = s.end - s.start;
        const double l = length(d);
        return l > 0 ? d * (1.0 / l) : Vec2{1, 0};
    }
    const Vec2 radial = (p - s.center) * (1.0 / s.radius);
    // Counter-clockwise travel: tangent = radial rotated +90 degrees; clockwise: -90 degrees.
    return s.sweep > 0 ? Vec2{-radial.y, radial.x} : Vec2{radial.y, -radial.x};
}
inline Vec2 tangentAtStart(const ContourSegmentInfo& s) noexcept { return tangentAt(s, s.start); }
inline Vec2 tangentAtEnd(const ContourSegmentInfo& s) noexcept { return tangentAt(s, s.end); }

/// Right-hand normal of a direction (the outward normal of a counter-clockwise contour).
inline Vec2 rightNormal(Vec2 t) noexcept { return {t.y, -t.x}; }

/// "(x, y)" with up to 3 decimals, for error messages.
inline std::string formatPoint(Vec2 p) {
    return "(" + core::formatLength(p.x, 3) + ", " + core::formatLength(p.y, 3) + ")";
}

} // namespace fos::geometry::detail
