// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/machining/face.hpp"
#include "fos/machining/validation.hpp"

#include <algorithm>
#include <cmath>

namespace fos::machining {
namespace {
using geometry::Vec3;

double comp(Vec3 v, int i) noexcept { return i == 0 ? v.x : (i == 1 ? v.y : v.z); }

/// Axis-parallel cylinder in the part frame.
struct Cyl {
    int axis = 2;        ///< 0 = X, 1 = Y, 2 = Z
    Vec3 centre;         ///< Any point on the axis (component `axis` unused).
    double lo = 0, hi = 0; ///< Extent along the axis.
    double r = 0;
};

Cyl toCyl(const domain::Part& part, const domain::Hole& h) noexcept {
    const HoleAxis a = holeAxis(part, h);
    Cyl c;
    c.axis = std::fabs(a.direction.x) > 0.5 ? 0 : (std::fabs(a.direction.y) > 0.5 ? 1 : 2);
    c.centre = a.start;
    const double s = comp(a.start, c.axis);
    const double e = comp(a.start + a.direction * a.length, c.axis);
    c.lo = std::min(s, e);
    c.hi = std::max(s, e);
    c.r = a.radius;
    return c;
}

/// Half-width of the set of offsets y (measured from the axis of a circle of radius r)
/// at which the chord [-h(y), h(y)], h = sqrt(r^2 - y^2), placed at `centre`, overlaps
/// the interval [lo, hi]. Returns a negative value when no offset qualifies.
double chordReach(double r, double centre, double lo, double hi) noexcept {
    const double need = std::max({centre - hi, lo - centre, 0.0}); // required half-chord
    if (need >= r) return -1.0;
    return std::sqrt(r * r - need * need);
}
} // namespace

bool holesCollide(const domain::Part& part, const domain::Hole& ha, const domain::Hole& hb) noexcept {
    const double eps = core::kLengthEps;
    const Cyl a = toCyl(part, ha), b = toCyl(part, hb);
    if (a.r <= 0 || b.r <= 0 || a.hi - a.lo <= 0 || b.hi - b.lo <= 0) return false;

    if (a.axis == b.axis) {
        // Parallel: circles overlap in the perpendicular plane and depth ranges overlap.
        const int i = (a.axis + 1) % 3, j = (a.axis + 2) % 3;
        const double d = std::hypot(comp(a.centre, i) - comp(b.centre, i), comp(a.centre, j) - comp(b.centre, j));
        const double overlap = std::min(a.hi, b.hi) - std::max(a.lo, b.lo);
        return d < a.r + b.r - eps && overlap > eps;
    }

    // Perpendicular: let c be the axis perpendicular to both cylinder axes. A common
    // point exists iff some coordinate along c lies in both cylinders' cross-sections
    // AND at that coordinate cylinder a's chord (along b's axis) reaches b's extent
    // while b's chord (along a's axis) reaches a's extent. Each condition restricts the
    // c-coordinate to an interval centred on the respective axis, so the test reduces
    // to an interval overlap.
    const int c = 3 - a.axis - b.axis;
    const double reachA = chordReach(a.r, comp(a.centre, b.axis), b.lo, b.hi);
    const double reachB = chordReach(b.r, comp(b.centre, a.axis), a.lo, a.hi);
    if (reachA < 0 || reachB < 0) return false;
    const double ca = comp(a.centre, c), cb = comp(b.centre, c);
    const double overlap = std::min(ca + reachA, cb + reachB) - std::max(ca - reachA, cb - reachB);
    return overlap > eps;
}

} // namespace fos::machining
