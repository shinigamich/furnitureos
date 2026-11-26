// SPDX-License-Identifier: AGPL-3.0-only
#include "pocket_gen.hpp"

#include <cmath>
#include <limits>
#include <numbers>
#include <vector>

namespace fos::cam::detail {

using core::ErrorCode;
using core::makeError;

namespace {

constexpr double kMinArcRadius = 1e-3; ///< below this a path corner is emitted sharp (simulator limit)
std::string mm(double v) { return core::formatLength(v, 3); }

/// Tool-centre region of the FINISHING pass: the pocket shrunk by r on closed sides and extended by
/// r + rho0 past open sides (so the rounded path corners there lie outside the stock); rho0 = path
/// corner radius = cornerRadius - r (>= 0).
struct Region {
    double a0, a1, b0, b1, rho0;
    double cx() const { return (a0 + a1) / 2; }
    double cy() const { return (b0 + b1) / 2; }
    double hx() const { return (a1 - a0) / 2; }
    double hy() const { return (b1 - b0) / 2; }
    double m() const { return std::min(hx(), hy()); }
    /// Length of the centre segment left when the region shrinks by m (0 for a square region).
    double segment() const { return 2 * std::fabs(hx() - hy()); }
};

Region region(const PocketTarget& t, double r) {
    Region g;
    g.rho0 = std::max(t.cornerRadius - r, 0.0);
    const double ext = r + g.rho0;
    g.a0 = t.open[0] ? t.x0 - ext : t.x0 + r;
    g.a1 = t.open[1] ? t.x1 + ext : t.x1 - r;
    g.b0 = t.open[2] ? t.y0 - ext : t.y0 + r;
    g.b1 = t.open[3] ? t.y1 + ext : t.y1 - r;
    return g;
}

bool canEnter(const PocketTarget& t, const Tool& tool) {
    return tool.centerCutting || region(t, tool.diameter / 2).segment() >= tool.diameter - core::kLengthEps;
}

std::string who(const PocketTarget& t, const domain::Part& part) {
    return "Pocket '" + t.id.str() + "' of '" + part.name + "'";
}

/// Closed loop at inset d from the finishing region, starting and ending at (cx, b0 + d).
void loop(Builder& b, const Region& g, double d, double z, bool ccw, double feed, const domain::OperationId& src) {
    using geometry::Vec2;
    const double A0 = g.a0 + d, A1 = g.a1 - d, B0 = g.b0 + d, B1 = g.b1 - d;
    double rho = g.rho0 - d;
    if (rho < kMinArcRadius) rho = 0;
    struct El {
        Vec2 p;
        bool arc;
        Vec2 c;
    };
    const Vec2 s{g.cx(), B0};
    const std::vector<El> ccwEls = {
        {{A1 - rho, B0}, false, {}}, {{A1, B0 + rho}, true, {A1 - rho, B0 + rho}},
        {{A1, B1 - rho}, false, {}}, {{A1 - rho, B1}, true, {A1 - rho, B1 - rho}},
        {{A0 + rho, B1}, false, {}}, {{A0, B1 - rho}, true, {A0 + rho, B1 - rho}},
        {{A0, B0 + rho}, false, {}}, {{A0 + rho, B0}, true, {A0 + rho, B0 + rho}},
        {s, false, {}}};
    std::vector<Vec2> pts{s};
    for (const auto& e : ccwEls) pts.push_back(e.p);
    const std::size_t n = ccwEls.size();
    for (std::size_t k = 0; k < n; ++k) {
        // CCW: element k runs pts[k] -> pts[k+1]; CW: element n-1-k reversed.
        const std::size_t e = ccw ? k : n - 1 - k;
        const Vec2 from = ccw ? pts[e] : pts[e + 1];
        const Vec2 to = ccw ? pts[e + 1] : pts[e];
        if (geometry::length(to - from) < 1e-9) continue;
        if (ccwEls[e].arc && rho > 0) b.arc({to.x, to.y, z}, ccwEls[e].c, ccw, feed, src);
        else b.linear({to.x, to.y, z}, feed, src);
    }
}

} // namespace

core::Result<const Tool*> selectPocketTool(const PocketTarget& t, const domain::Part& part, const ToolLibrary& tools,
                                           const CamOptions& o) {
    const double depth = t.topZ - t.floorZ;
    const bool corner = t.hasInnerCorner();
    double dMax = t.closedSpan();
    if (corner) dMax = std::min(dMax, 2 * t.cornerRadius);
    std::string limit;
    if (corner)
        limit = std::string("2 x the ") + (t.radiusFromModel ? "pocket's" : "accepted") + " inner corner radius " +
                mm(t.cornerRadius) + " mm";
    if (std::isfinite(t.closedSpan()) && (!corner || t.closedSpan() < 2 * t.cornerRadius))
        limit = "the narrowest closed width " + mm(t.closedSpan()) + " mm";
    auto fits = [&](const Tool& tl, std::string& why) {
        if (tl.kind != ToolKind::EndMill) why = "it is not a flat end mill";
        else if (tl.diameter > dMax + core::kLengthEps)
            why = "its diameter " + mm(tl.diameter) + " mm exceeds " + mm(dMax) + " mm (" + limit + ")";
        else if (tl.fluteLength + core::kLengthEps < depth)
            why = "its flute length " + mm(tl.fluteLength) + " mm is shorter than the depth " + mm(depth) + " mm";
        else if (!canEnter(t, tl))
            why = "it is not centre-cutting and the pocket leaves no straight run of at least its diameter for a "
                  "ramped entry";
        else return true;
        return false;
    };
    std::string why;
    if (o.pocketToolNumber != 0) {
        const Tool* tl = tools.findByNumber(o.pocketToolNumber);
        if (!tl)
            return makeError(ErrorCode::NotFound, who(t, part) + ": pocket tool T" +
                                                      std::to_string(o.pocketToolNumber) + " is not in the tool library.",
                             part.id.str());
        if (!fits(*tl, why))
            return makeError(ErrorCode::ConstraintViolation,
                             who(t, part) + ": tool '" + tl->id + "' (T" + std::to_string(tl->number) +
                                 ") cannot mill it: " + why + ".", part.id.str());
        return tl;
    }
    const Tool* best = nullptr;
    std::string avail;
    for (const auto& tl : tools.tools()) {
        if (tl.kind != ToolKind::EndMill) continue;
        avail += (avail.empty() ? "" : ", ") + tl.id + " " + mm(tl.diameter) + " mm";
        if (fits(tl, why) && (!best || core::definitelyGreater(tl.diameter, best->diameter))) best = &tl;
    }
    if (best) return best;
    return makeError(ErrorCode::NotFound,
                     who(t, part) + " needs a flat end mill of diameter <= " + mm(dMax) + " mm (" + limit +
                         ") with flute length >= " + mm(depth) +
                         " mm that is centre-cutting or has room to ramp; none in the tool library fits (end mills: " +
                         (avail.empty() ? std::string("none") : avail) + ").",
                     part.id.str());
}

core::Result<void> millPocket(Builder& b, const PocketTarget& t, const Tool& tool, const domain::Part& part,
                              const CamOptions& o) {
    using geometry::Vec2;
    const double r = tool.diameter / 2;
    const double step = o.pocketStepoverFraction * tool.diameter;
    const double fa = o.pocketFinishAllowance;
    if (!(fa < step))
        return makeError(ErrorCode::InvalidArgument,
                         who(t, part) + ": the finish allowance " + mm(fa) + " mm must be smaller than the stepover " +
                             mm(step) + " mm (" + mm(o.pocketStepoverFraction * 100) + " % of the " +
                             mm(tool.diameter) + " mm tool).", part.id.str());
    const Region g = region(t, r);
    const domain::OperationId& src = t.id;
    const bool ccw = o.contourDirection == ContourDirection::Climb; // spindle CW: climb = wall on the right
    const double m = g.m();
    const bool alongX = g.hx() >= g.hy();
    const double half = g.segment() / 2;
    const Vec2 p0{g.cx(), g.cy()};
    const Vec2 ePlus = alongX ? Vec2{g.cx() + half, g.cy()} : Vec2{g.cx(), g.cy() + half};
    const Vec2 eMinus = alongX ? Vec2{g.cx() - half, g.cy()} : Vec2{g.cx(), g.cy() - half};
    if (!canEnter(t, tool))
        return makeError(ErrorCode::ConstraintViolation,
                         who(t, part) + ": tool '" + tool.id + "' is not centre-cutting and the " + mm(2 * half) +
                             " mm straight run is too short for a ramped entry (needs >= " + mm(tool.diameter) + " mm).",
                         part.id.str());
    // Roughing insets from the centre (m) out to the finish allowance.
    std::vector<double> insets{m};
    if (m > fa + core::kLengthEps) {
        const int n = static_cast<int>(std::ceil((m - fa) / step - 1e-9));
        for (int k = 1; k <= n; ++k) insets.push_back(m - (m - fa) * k / n);
    }
    std::vector<double> levels;
    const double depth = t.topZ - t.floorZ;
    for (int k = 1;; ++k) {
        const double d = k * tool.maxDepthOfCut;
        if (d >= depth - core::kLengthEps) break;
        levels.push_back(t.topZ - d);
    }
    levels.push_back(t.floorZ);
    const double tanA = std::tan(o.pocketRampAngle * std::numbers::pi / 180.0);
    double zPrev = t.topZ;
    for (double z : levels) {
        b.moveAbove(p0, src);
        if (tool.centerCutting) {
            b.linear({p0.x, p0.y, z}, tool.plungeRate, src);
        } else {
            // Feed down through the already cleared column, then zigzag ramp along the centre segment.
            b.linear({p0.x, p0.y, zPrev}, tool.plungeRate, src);
            const double run = (zPrev - z) / tanA;
            double done = 0;
            Vec2 at = p0, next = ePlus;
            while (true) {
                const double leg = geometry::length(next - at);
                if (done + leg >= run - core::kLengthEps) {
                    const double f = leg > 0 ? (run - done) / leg : 0;
                    const Vec2 q = at + (next - at) * f;
                    b.linear({q.x, q.y, z}, tool.feedRate, src);
                    break;
                }
                done += leg;
                b.linear({next.x, next.y, zPrev - done * tanA}, tool.feedRate, src);
                at = next;
                next = (next.x == ePlus.x && next.y == ePlus.y) ? eMinus : ePlus;
            }
        }
        if (half > core::kLengthEps) {
            b.linear({ePlus.x, ePlus.y, z}, tool.feedRate, src);
            b.linear({eMinus.x, eMinus.y, z}, tool.feedRate, src);
            b.linear({p0.x, p0.y, z}, tool.feedRate, src);
        }
        for (std::size_t k = 1; k < insets.size(); ++k) {
            b.linear({g.cx(), g.b0 + insets[k], z}, tool.feedRate, src);
            loop(b, g, insets[k], z, ccw, tool.feedRate, src);
        }
        b.retract(src);
        zPrev = z;
    }
    // Finishing: one wall loop per level, entered radially from the cleared side.
    const Vec2 fin{g.cx(), g.b0 + std::min(fa, m)};
    for (double z : levels) {
        b.moveAbove(fin, src);
        b.linear({fin.x, fin.y, z}, tool.plungeRate, src);
        b.linear({g.cx(), g.b0, z}, tool.feedRate, src);
        loop(b, g, 0, z, ccw, tool.feedRate, src);
        b.linear({fin.x, fin.y, z}, tool.feedRate, src);
        b.retract(src);
    }
    return {};
}

} // namespace fos::cam::detail
