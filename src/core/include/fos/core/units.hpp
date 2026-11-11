// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <cmath>
#include <string>

/// Unit and precision policy (see docs/decisions/ADR-0003-units-and-precision.md):
///  * All lengths in the engineering model are millimetres stored as double.
///  * All angles in the model are degrees; trigonometric code converts explicitly.
///  * Intermediate results are never rounded. Rounding happens only for display,
///    reports and export, through the helpers below.
namespace fos::core {

inline constexpr double kMmPerInch = 25.4;
inline constexpr double kPi = 3.14159265358979323846;

/// Two lengths closer than this are considered equal (1 nanometre). Far below any
/// manufacturing tolerance, far above double rounding noise for furniture sizes.
inline constexpr double kLengthEps = 1e-6;
/// Default precision for presenting dimensions to people (0.1 mm).
inline constexpr int kDisplayDecimals = 1;

constexpr double inchesToMm(double in) noexcept { return in * kMmPerInch; }
constexpr double mmToInches(double mm) noexcept { return mm / kMmPerInch; }
constexpr double cmToMm(double cm) noexcept { return cm * 10.0; }
constexpr double mToMm(double m) noexcept { return m * 1000.0; }
constexpr double mmToM(double mm) noexcept { return mm / 1000.0; }
constexpr double degToRad(double deg) noexcept { return deg * kPi / 180.0; }
constexpr double radToDeg(double rad) noexcept { return rad * 180.0 / kPi; }

inline bool approxEqual(double a, double b, double eps = kLengthEps) noexcept {
    return std::fabs(a - b) <= eps;
}
inline bool approxZero(double a, double eps = kLengthEps) noexcept { return std::fabs(a) <= eps; }
/// a < b by more than eps.
inline bool definitelyLess(double a, double b, double eps = kLengthEps) noexcept { return a < b - eps; }
/// a > b by more than eps.
inline bool definitelyGreater(double a, double b, double eps = kLengthEps) noexcept { return a > b + eps; }

/// Round half away from zero to `decimals` decimal places.
double roundTo(double value, int decimals) noexcept;

/// Locale-independent formatting for reports/export: rounds to `decimals`, strips
/// trailing zeros ("764", "719.2"), never prints "-0", always uses '.'.
std::string formatLength(double mm, int decimals = kDisplayDecimals);

} // namespace fos::core
