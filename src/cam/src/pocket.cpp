// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/cam/pocket.hpp"

#include "fos/core/units.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace fos::cam {

namespace {

constexpr double kEps = 1e-6;
std::string mm(double v) { return core::formatLength(v, 3); }

struct FaceRect {
    double u0, v0, u1, v1;
};

/// The contour as an axis-parallel rectangle in face coordinates, or nullopt.
std::optional<FaceRect> asRectangle(const std::vector<geometry::Vec2>& c) {
    if (c.size() != 4) return std::nullopt;
    FaceRect r{c[0].x, c[0].y, c[0].x, c[0].y};
    for (const auto& p : c) {
        r.u0 = std::min(r.u0, p.x); r.u1 = std::max(r.u1, p.x);
        r.v0 = std::min(r.v0, p.y); r.v1 = std::max(r.v1, p.y);
    }
    if (r.u1 - r.u0 < kEps || r.v1 - r.v0 < kEps) return std::nullopt;
    bool seen[4] = {false, false, false, false};
    for (std::size_t i = 0; i < 4; ++i) {
        const auto& p = c[i];
        const bool lu = std::fabs(p.x - r.u0) < kEps, hu = std::fabs(p.x - r.u1) < kEps;
        const bool lv = std::fabs(p.y - r.v0) < kEps, hv = std::fabs(p.y - r.v1) < kEps;
        if (!(lu || hu) || !(lv || hv)) return std::nullopt;
        seen[(hu ? 1 : 0) + (hv ? 2 : 0)] = true;
        const auto& q = c[(i + 1) % 4];
        if (std::fabs(p.x - q.x) > kEps && std::fabs(p.y - q.y) > kEps) return std::nullopt; // diagonal edge
    }
    if (!(seen[0] && seen[1] && seen[2] && seen[3])) return std::nullopt;
    return r;
}

/// Machine rectangle and side flags "on the part boundary" (unchecked for blank allowance).
PocketTarget rawTarget(const domain::Part& part, const SetupFrame& frame, const domain::Operation& op,
                       const FaceRect& fr) {
    PocketTarget t;
    t.id = op.id;
    const geometry::Vec2 a = faceToMachineXY(part, frame, {fr.u0, fr.v0});
    const geometry::Vec2 b = faceToMachineXY(part, frame, {fr.u1, fr.v1});
    t.x0 = std::min(a.x, b.x); t.x1 = std::max(a.x, b.x);
    t.y0 = std::min(a.y, b.y); t.y1 = std::max(a.y, b.y);
    const double px0 = frame.origin.x, px1 = frame.origin.x + frame.length;
    const double py0 = frame.origin.y, py1 = frame.origin.y + frame.width;
    t.open[0] = std::fabs(t.x0 - px0) < kEps;
    t.open[1] = std::fabs(t.x1 - px1) < kEps;
    t.open[2] = std::fabs(t.y0 - py0) < kEps;
    t.open[3] = std::fabs(t.y1 - py1) < kEps;
    t.topZ = frame.topZ;
    t.floorZ = frame.topZ - std::get<domain::Pocket>(op.shape).depth;
    return t;
}

} // namespace

bool PocketTarget::hasInnerCorner() const noexcept {
    return (!open[0] || !open[1]) && (!open[2] || !open[3]);
}

double PocketTarget::closedSpan() const noexcept {
    double s = std::numeric_limits<double>::infinity();
    if (!open[0] && !open[1]) s = std::min(s, x1 - x0);
    if (!open[2] && !open[3]) s = std::min(s, y1 - y0);
    return s;
}

bool PocketTarget::allows(geometry::Vec2 p, double tol) const noexcept {
    return (open[0] || p.x >= x0 - tol) && (open[1] || p.x <= x1 + tol) && (open[2] || p.y >= y0 - tol) &&
           (open[3] || p.y <= y1 + tol);
}

bool PocketTarget::expects(geometry::Vec2 p, double tol) const noexcept {
    if (!((open[0] || p.x >= x0 + tol) && (open[1] || p.x <= x1 - tol) && (open[2] || p.y >= y0 + tol) &&
          (open[3] || p.y <= y1 - tol)))
        return false;
    const double R = cornerRadius;
    if (R <= 0) return true;
    for (int sx = 0; sx < 2; ++sx)
        for (int sy = 2; sy < 4; ++sy) {
            if (open[sx] || open[sy]) continue; // not an inner corner
            const double cx = sx == 0 ? x0 + R : x1 - R, cy = sy == 2 ? y0 + R : y1 - R;
            const bool inCornerX = sx == 0 ? p.x < cx : p.x > cx;
            const bool inCornerY = sy == 2 ? p.y < cy : p.y > cy;
            if (inCornerX && inCornerY && std::hypot(p.x - cx, p.y - cy) > R - tol) return false;
        }
    return true;
}

double effectivePocketCornerRadius(const domain::Pocket& pocket, double acceptedRadius) noexcept {
    return pocket.cornerRadius > 0 ? pocket.cornerRadius : acceptedRadius;
}

std::optional<std::string> pocketUnsupportedReason(const domain::Part& part, const SetupFrame& frame,
                                                   const domain::Operation& op, double cornerRadius) {
    const auto& pk = std::get<domain::Pocket>(op.shape);
    const std::string name = "Pocket '" + op.id.str() + "' on face " + domain::toString(pk.face) + " of '" +
                             part.name + "'";
    if (pk.through)
        return name + " is a through pocket (a cut-out); only blind pockets are milled. Model it as a cut-out of "
                      "the part shape (routed as a contour) or machine it by other means.";
    const auto fr = asRectangle(pk.contour);
    if (!fr)
        return name + " is not an axis-parallel rectangle (" + std::to_string(pk.contour.size()) +
               " vertices); only rectangular pockets are milled.";
    if (fr->u0 < -kEps || fr->v0 < -kEps || fr->u1 > part.length + kEps || fr->v1 > part.width + kEps)
        return name + " extends beyond the face of the part.";
    const PocketTarget t = rawTarget(part, frame, op, *fr);
    const bool anyOpen = t.open[0] || t.open[1] || t.open[2] || t.open[3];
    if (t.open[0] && t.open[1] && t.open[2] && t.open[3])
        return name + " covers the whole face (open on all four sides); that is a thickness reduction, not a pocket.";
    if (anyOpen && frame.blankAllowance > 0)
        return name + " is open to the part edge, but the part is clamped as a blank with " +
               mm(frame.blankAllowance) + " mm allowance, so that edge is not the stock edge; open pockets are "
               "only milled on stock without blank allowance.";
    if (t.hasInnerCorner()) {
        const bool fromModel = pk.cornerRadius > 0;
        const double radius = effectivePocketCornerRadius(pk, cornerRadius);
        if (!(radius > 0))
            return name + " has sharp inner corners, which no end mill can cut, and the domain pocket carries no "
                          "corner radius (Pocket::cornerRadius is 0). Give the pocket a corner radius, or set "
                          "CamOptions::pocketCornerRadius (and SimulationOptions::pocketCornerRadius) to the inner "
                          "corner radius you accept for sharp pockets; the end mill diameter is then limited to 2 x "
                          "that radius.";
        double span = std::numeric_limits<double>::infinity();
        if (!t.open[0] || !t.open[1]) span = std::min(span, t.x1 - t.x0);
        if (!t.open[2] || !t.open[3]) span = std::min(span, t.y1 - t.y0);
        if (2 * radius > span + kEps)
            return name + ": the " + (fromModel ? "pocket's corner radius " : "accepted corner radius ") + mm(radius) +
                   " mm is more than half its " + mm(span) + " mm side.";
    }
    return std::nullopt;
}

PocketTarget pocketTarget(const domain::Part& part, const SetupFrame& frame, const domain::Operation& op,
                          double cornerRadius) {
    const auto fr = asRectangle(std::get<domain::Pocket>(op.shape).contour);
    PocketTarget t = rawTarget(part, frame, op, fr.value_or(FaceRect{0, 0, 0, 0}));
    const auto& pk = std::get<domain::Pocket>(op.shape);
    t.cornerRadius = t.hasInnerCorner() ? effectivePocketCornerRadius(pk, cornerRadius) : 0;
    t.radiusFromModel = t.hasInnerCorner() && pk.cornerRadius > 0;
    return t;
}

} // namespace fos::cam
