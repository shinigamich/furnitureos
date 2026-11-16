// SPDX-License-Identifier: AGPL-3.0-only
#include "contour_gen.hpp"

#include "arc_math.hpp"

#include <algorithm>
#include <cmath>

namespace fos::cam::detail {

using core::ErrorCode;
using core::makeError;

namespace {

std::string mm(double v) { return core::formatLength(v, 3); }

std::string contourName(const ContourTarget& t, const domain::Part& part) {
    return (t.outer ? std::string("The outline") : "Cut-out " + std::to_string(t.cutoutIndex)) + " ('" +
           t.id.str() + "') of '" + part.name + "'";
}

Vec2 unit(Vec2 v) { return v * (1.0 / geometry::length(v)); }
Vec2 leftOf(Vec2 t) { return {-t.y, t.x}; }
Vec2 rightOf(Vec2 t) { return {t.y, -t.x}; }

/// Travel direction of segment i of `c` at its start (atStart) or end.
Vec2 tangent(const geometry::Contour& c, std::size_t i, bool atStart) {
    const geometry::ContourSegmentInfo s = geometry::segmentInfo(c, i);
    if (!s.isArc) return unit(s.end - s.start);
    const Vec2 u = unit((atStart ? s.start : s.end) - s.center);
    return s.sweep > 0 ? leftOf(u) : rightOf(u);
}

/// Sharp corners whose waste side is a wedge the tool cannot reach (machine contour, CCW):
/// right turns of the outline (inside corners of the part), left turns of a cut-out.
std::vector<Vec2> unreachableCorners(const ContourTarget& t) {
    std::vector<Vec2> out;
    const std::size_t n = t.contour.size();
    for (std::size_t i = 0; i < n; ++i) {
        const Vec2 tin = tangent(t.contour, (i + n - 1) % n, false), tout = tangent(t.contour, i, true);
        const double cr = geometry::cross(tin, tout);
        if (std::fabs(cr) < 1e-6 && geometry::dot(tin, tout) > 0) continue; // tangent junction
        if (t.outer ? cr < 0 : cr > 0) out.push_back(t.contour.vertices[i].point);
    }
    return out;
}

/// Tool-centre path (counter-clockwise, as offsetContour returns it).
core::Result<geometry::Contour> toolPath(const ContourTarget& t, double r) {
    return geometry::offsetContour(t.contour, t.outer ? r : -r);
}

struct Piece {
    Vec2 end;
    bool arc = false;
    Vec2 centre;
    bool ccw = false;
};

/// Loop over the traversal contour starting and ending at the midpoint S of segment k.
std::vector<Piece> loopPieces(const geometry::Contour& c, std::size_t k, Vec2& start, Vec2& startTangent) {
    const std::size_t n = c.size();
    std::vector<Piece> out;
    const geometry::ContourSegmentInfo sk = geometry::segmentInfo(c, k);
    if (sk.isArc) {
        start = arcPoint(segmentArc(c, k), 0.5);
        const Vec2 u = unit(start - sk.center);
        startTangent = sk.sweep > 0 ? leftOf(u) : rightOf(u);
    } else {
        start = (sk.start + sk.end) * 0.5;
        startTangent = unit(sk.end - sk.start);
    }
    auto add = [&](const geometry::ContourSegmentInfo& s, Vec2 end) {
        out.push_back({end, s.isArc, s.center, s.sweep > 0});
    };
    add(sk, sk.end);
    for (std::size_t j = 1; j < n; ++j) {
        const geometry::ContourSegmentInfo s = geometry::segmentInfo(c, (k + j) % n);
        add(s, s.end);
    }
    add(sk, start);
    return out;
}

/// Longest straight segment, or the longest segment when none is straight (first on ties).
std::size_t startSegment(const geometry::Contour& c) {
    std::size_t best = 0;
    double bestLen = -1;
    bool bestLine = false;
    for (std::size_t i = 0; i < c.size(); ++i) {
        const geometry::ContourSegmentInfo s = geometry::segmentInfo(c, i);
        const bool line = !s.isArc;
        if ((line && !bestLine) || (line == bestLine && s.length > bestLen + 1e-9)) {
            best = i;
            bestLen = s.length;
            bestLine = line;
        }
    }
    return best;
}

core::Result<const Tool*> selectTool(const std::vector<ContourTarget>& targets, const domain::Part& part,
                                     const ToolLibrary& tools, const CamOptions& o, double depth) {
    auto suitable = [&](const Tool& t, std::string& why) {
        if (t.kind != ToolKind::EndMill || !t.centerCutting) {
            why = "'" + t.id + "' is not a centre-cutting end mill";
            return false;
        }
        if (t.fluteLength < depth - core::kLengthEps) {
            why = "'" + t.id + "' has flute length " + mm(t.fluteLength) + " mm but " + mm(depth) +
                  " mm of cutting depth is needed";
            return false;
        }
        for (const auto& tg : targets)
            if (auto p = toolPath(tg, t.diameter / 2); !p) {
                why = "'" + t.id + "' (" + mm(t.diameter) + " mm) cannot follow " + contourName(tg, part) + ": " +
                      p.error().message;
                return false;
            }
        return true;
    };
    std::string why;
    if (o.contourToolNumber != 0) {
        const Tool* t = tools.findByNumber(o.contourToolNumber);
        if (!t)
            return makeError(ErrorCode::NotFound, "Contour tool T" + std::to_string(o.contourToolNumber) +
                                                      " requested for '" + part.name + "' is not in the tool library.",
                             part.id.str());
        if (!suitable(*t, why))
            return makeError(ErrorCode::ConstraintViolation,
                             "Contour tool T" + std::to_string(o.contourToolNumber) + " cannot route '" + part.name +
                                 "': " + why + ".", part.id.str());
        return t;
    }
    std::vector<const Tool*> cand;
    for (const auto& t : tools.tools()) cand.push_back(&t);
    std::stable_sort(cand.begin(), cand.end(), [](const Tool* a, const Tool* b) {
        if (a->diameter != b->diameter) return a->diameter > b->diameter;
        return a->number < b->number;
    });
    std::string tried;
    for (const Tool* t : cand) {
        if (suitable(*t, why)) return t;
        tried += (tried.empty() ? "" : "; ") + why;
    }
    return makeError(ErrorCode::NotFound, "No tool in the library can route the contours of '" + part.name + "'" +
                                              (tried.empty() ? std::string(" (the library is empty).")
                                                             : ": " + tried + "."),
                     part.id.str());
}

} // namespace

core::Result<void> routeContours(Builder& b, const std::vector<ContourTarget>& targets, const domain::Part& part,
                                 const SetupFrame& frame, const MachineProfile& machine, const ToolLibrary& tools,
                                 const CamOptions& o, CamResult& res) {
    if (targets.empty()) return {};
    if (o.breakthroughAllowance > machine.maxSpoilboardPenetration + core::kLengthEps)
        return makeError(ErrorCode::ConstraintViolation,
                         "Contour routing of '" + part.name + "': breakthrough allowance " +
                             mm(o.breakthroughAllowance) + " mm exceeds the spoilboard limit " +
                             mm(machine.maxSpoilboardPenetration) + " mm of machine '" + machine.name + "'.",
                         part.id.str());
    const double floorZ = frame.bottomZ - o.breakthroughAllowance;
    const double depth = frame.topZ - floorZ;
    auto toolR = selectTool(targets, part, tools, o, depth);
    if (!toolR) return toolR.error();
    const Tool& tool = *toolR.value();
    if (auto lim = checkToolLimits(tool, machine); !lim) return lim.error();
    const double r = tool.diameter / 2;
    const double rho = o.contourLeadRadius > 0 ? o.contourLeadRadius : r;
    const bool climb = o.contourDirection == ContourDirection::Climb;

    std::vector<double> levels;
    for (int k = 1;; ++k) {
        const double d = k * tool.maxDepthOfCut;
        if (d >= depth - core::kLengthEps) break;
        levels.push_back(frame.topZ - d);
    }
    levels.push_back(floorZ);

    b.useTool(tool);
    if (std::find(res.toolsUsed.begin(), res.toolsUsed.end(), tool.number) == res.toolsUsed.end())
        res.toolsUsed.push_back(tool.number);
    for (const ContourTarget& t : targets) {
        const auto corners = unreachableCorners(t);
        if (!corners.empty())
            res.diagnostics.push_back(
                {core::Severity::Warning, "cam.contour.rounded_corner",
                 contourName(t, part) + " has " + std::to_string(corners.size()) +
                     " sharp corner(s) the end mill cannot reach; they are left rounded with radius " + mm(r) +
                     " mm (first at machine X " + mm(corners[0].x) + ", Y " + mm(corners[0].y) + ").",
                 part.id.str()});
        auto pathR = toolPath(t, r);
        if (!pathR) return pathR.error(); // selectTool checked it; kept for safety
        // Climb (spindle CW): part on the right of the tool -> outline clockwise, cut-outs CCW.
        const bool reverse = t.outer == climb;
        const geometry::Contour path = reverse ? geometry::reversed(pathR.value()) : pathR.value();
        Vec2 s, tan;
        const std::vector<Piece> pieces = loopPieces(path, startSegment(path), s, tan);
        // Waste side: left of travel for climb, right for conventional (part on the other side).
        const Vec2 w = climb ? leftOf(tan) : rightOf(tan);
        const Vec2 c = s + w * rho;
        const Vec2 p0 = c - tan * rho, p1 = c + tan * rho;
        const bool leadCcw = climb; // quarter circles tangent to the path at s (see generate.hpp)
        for (double z : levels) {
            b.moveAbove(p0, t.id);
            b.linear({p0.x, p0.y, z}, tool.plungeRate, t.id);
            b.arc({s.x, s.y, z}, c, leadCcw, tool.feedRate, t.id);
            for (const Piece& p : pieces) {
                if (p.arc) b.arc({p.end.x, p.end.y, z}, p.centre, p.ccw, tool.feedRate, t.id);
                else b.linear({p.end.x, p.end.y, z}, tool.feedRate, t.id);
            }
            b.arc({p1.x, p1.y, z}, c, leadCcw, tool.feedRate, t.id);
            b.retract(t.id);
        }
    }
    return {};
}

} // namespace fos::cam::detail
