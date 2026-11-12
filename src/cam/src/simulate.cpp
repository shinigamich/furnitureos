// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/cam/simulate.hpp"

#include "heightmap.hpp"

#include "fos/core/units.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>

namespace fos::cam {

using geometry::Vec2;
using geometry::Vec3;

namespace {

constexpr double kEps = 1e-6;
std::string mm(double v) { return core::formatLength(v, 3); }

struct Rect {
    double x0, y0, x1, y1;
};

double pointRectDistance(Vec2 p, const Rect& r) {
    const double dx = std::max({r.x0 - p.x, 0.0, p.x - r.x1});
    const double dy = std::max({r.y0 - p.y, 0.0, p.y - r.y1});
    return std::hypot(dx, dy);
}

double pointSegmentDistance(Vec2 p, Vec2 a, Vec2 b) {
    const Vec2 d = b - a;
    const double dd = geometry::dot(d, d);
    const double t = dd < 1e-18 ? 0.0 : std::clamp(geometry::dot(p - a, d) / dd, 0.0, 1.0);
    return geometry::length(p - (a + d * t));
}

/// Liang-Barsky: does segment a-b intersect the closed rectangle?
bool segmentHitsRect(Vec2 a, Vec2 b, const Rect& r) {
    double t0 = 0, t1 = 1;
    const Vec2 d = b - a;
    const double p[4] = {-d.x, d.x, -d.y, d.y};
    const double q[4] = {a.x - r.x0, r.x1 - a.x, a.y - r.y0, r.y1 - a.y};
    for (int k = 0; k < 4; ++k) {
        if (std::fabs(p[k]) < 1e-18) {
            if (q[k] < 0) return false;
        } else {
            const double t = q[k] / p[k];
            if (p[k] < 0) t0 = std::max(t0, t);
            else t1 = std::min(t1, t);
            if (t0 > t1) return false;
        }
    }
    return true;
}

/// True when a tool of radius r moving along a-b (XY) comes over the rectangle.
bool overStock(Vec2 a, Vec2 b, double r, const Rect& s) {
    if (segmentHitsRect(a, b, s)) return true;
    double d = std::min(pointRectDistance(a, s), pointRectDistance(b, s));
    for (Vec2 c : {Vec2{s.x0, s.y0}, Vec2{s.x1, s.y0}, Vec2{s.x0, s.y1}, Vec2{s.x1, s.y1}})
        d = std::min(d, pointSegmentDistance(c, a, b));
    return d < r - kEps;
}

struct Target {
    std::string id;
    bool hole = true;
    // hole
    Vec2 centre;
    double radius = 0;
    // groove
    Vec2 start, dir;
    double length = 0, halfWidth = 0;
    bool startOpen = false, endOpen = false;
    double expectedFloor = 0; ///< must be cut down to this
    double allowedFloor = 0;  ///< must not be cut below this
};

/// longitudinal / lateral coordinates of p relative to a groove target
void grooveCoords(const Target& t, Vec2 p, double& s, double& lat) {
    const Vec2 q = p - t.start;
    s = geometry::dot(q, t.dir);
    lat = geometry::cross(t.dir, q);
}

bool inAllowed(const Target& t, Vec2 p) {
    constexpr double tol = 1e-4;
    if (t.hole) return geometry::length(p - t.centre) <= t.radius + tol;
    double s, lat;
    grooveCoords(t, p, s, lat);
    if (std::fabs(lat) > t.halfWidth + tol) return false;
    if (!t.startOpen && s < -tol) return false;
    if (!t.endOpen && s > t.length + tol) return false;
    return true;
}

bool inExpected(const Target& t, Vec2 p) {
    constexpr double tol = 1e-4;
    if (t.hole) return geometry::length(p - t.centre) <= t.radius - tol;
    double s, lat;
    grooveCoords(t, p, s, lat);
    if (std::fabs(lat) > t.halfWidth - tol) return false;
    const double lo = t.startOpen ? -1e300 : t.halfWidth + tol;
    const double hi = t.endOpen ? 1e300 : t.length - t.halfWidth - tol;
    return s >= lo && s <= hi;
}

struct Agg {
    std::size_t count = 0;
    double x0 = 1e300, y0 = 1e300, x1 = -1e300, y1 = -1e300, minZ = 1e300, floor = 0;
    std::size_t firstMotion = 0;
    void add(Vec2 c, double z, std::size_t motion) {
        if (count++ == 0) firstMotion = motion;
        x0 = std::min(x0, c.x);
        y0 = std::min(y0, c.y);
        x1 = std::max(x1, c.x);
        y1 = std::max(y1, c.y);
        minZ = std::min(minZ, z);
    }
};

} // namespace

SimulationReport simulate(const Program& program, const domain::Part& part, const Setup& setup,
                          const MachineProfile& machine, const ToolLibrary& tools, const SimulationOptions& options) {
    SimulationReport rep;
    const std::string pid = part.id.str();
    auto error = [&](const char* code, std::string msg) {
        rep.diagnostics.push_back({core::Severity::Error, code, std::move(msg), pid});
    };
    auto frameR = makeSetupFrame(part, setup);
    if (!frameR) {
        error("cam.sim.setup_mismatch", frameR.error().message);
        return rep;
    }
    const SetupFrame& frame = frameR.value();
    if (program.faceUp != setup.faceUp)
        error("cam.sim.setup_mismatch", std::string("The program was generated for face ") +
                                            domain::toString(program.faceUp) + " up but the setup has face " +
                                            domain::toString(setup.faceUp) + " up.");
    if (!(options.cellSize > 0)) {
        error("cam.sim.setup_mismatch", "Simulation cell size must be greater than 0.");
        return rep;
    }
    const double clearZ = frame.topZ + machine.clearance;
    const double spoilLimit = frame.bottomZ - machine.maxSpoilboardPenetration;
    const Rect stock{frame.stock.min.x, frame.stock.min.y, frame.stock.max.x, frame.stock.max.y};
    detail::Heightmap hm(stock.x0, stock.y0, frame.length, frame.width, options.cellSize, frame.topZ);

    Vec3 pos;
    bool xyKnown = false, zKnown = false, spindle = false;
    const Tool* tool = nullptr;
    auto at = [&](std::size_t i) { return "Motion #" + std::to_string(i) + " (" + describe(program.motions[i]) + ")"; };

    for (std::size_t i = 0; i < program.motions.size(); ++i) {
        const Motion& m = program.motions[i];
        switch (m.kind) {
        case MotionKind::Rapid:
        case MotionKind::Linear: {
            const bool rapid = m.kind == MotionKind::Rapid;
            if (!machine.envelopeZ.contains(m.target.z) ||
                (!m.zOnly && (!machine.envelopeX.contains(m.target.x) || !machine.envelopeY.contains(m.target.y))))
                error("cam.sim.envelope", at(i) + " leaves the envelope of machine '" + machine.name + "' (X [" +
                                              mm(machine.envelopeX.min) + ", " + mm(machine.envelopeX.max) + "], Y [" +
                                              mm(machine.envelopeY.min) + ", " + mm(machine.envelopeY.max) + "], Z [" +
                                              mm(machine.envelopeZ.min) + ", " + mm(machine.envelopeZ.max) + "] mm).");
            Vec3 target = m.target;
            if (m.zOnly) {
                if (!rapid) error("cam.sim.position_unknown", at(i) + " is a Z-only linear move, which is not allowed.");
                if (!xyKnown) {
                    if (m.target.z < machine.safeZ - kEps)
                        error("cam.sim.position_unknown", at(i) + " retracts from an unknown position to Z " +
                                                              mm(m.target.z) + " mm, below safe Z " +
                                                              mm(machine.safeZ) + " mm.");
                    if (zKnown) rep.rapidLength += std::fabs(m.target.z - pos.z);
                    pos.z = m.target.z;
                    zKnown = true;
                    break;
                }
                target = {pos.x, pos.y, m.target.z};
            } else if (!xyKnown || !zKnown) {
                // From an unknown X/Y only horizontal travel at or above safe Z is allowed.
                const bool safeTravel = rapid && zKnown && pos.z >= machine.safeZ - kEps &&
                                        core::approxEqual(m.target.z, pos.z);
                if (!safeTravel)
                    error("cam.sim.position_unknown",
                          at(i) + " moves in X/Y before the tool position is known; first retract to safe Z with a "
                                  "Z-only move, then travel horizontally at that height.");
                pos = m.target;
                xyKnown = zKnown = true;
                break;
            }
            const Vec3 from = pos;
            const double len = geometry::length(target - from);
            const double r = tool ? tool->diameter / 2 : 0.0;
            const Vec2 a{from.x, from.y}, b{target.x, target.y};
            const double lowZ = std::min(from.z, target.z);
            const bool over = overStock(a, b, std::max(r, kEps * 2), stock);
            if (lowZ < spoilLimit - kEps)
                error("cam.sim.spoilboard_limit", at(i) + " reaches Z " + mm(lowZ) + " mm, " +
                                                      mm(frame.bottomZ - lowZ) +
                                                      " mm below the stock bottom; the limit is " +
                                                      mm(machine.maxSpoilboardPenetration) + " mm.");
            if (rapid) {
                const bool upward = core::approxEqual(a.x, b.x) && core::approxEqual(a.y, b.y) && target.z >= from.z - kEps;
                if (!upward && over && lowZ < clearZ - kEps)
                    error("cam.sim.rapid_below_clearance", at(i) + " is a rapid move over the stock down to Z " +
                                                               mm(lowZ) + " mm, below the clearance plane Z " +
                                                               mm(clearZ) + " mm.");
                rep.rapidLength += len;
                if (machine.rapidFeed > 0) rep.estimatedSeconds += len / machine.rapidFeed * 60.0;
            } else {
                if (!tool) error("cam.sim.no_tool", at(i) + " is a cutting move with no tool loaded.");
                if (!spindle) error("cam.sim.spindle_off_cut", at(i) + " is a cutting move while the spindle is off.");
                if (!(m.feed > 0) || m.feed > machine.maxFeed + kEps)
                    error("cam.sim.feed_limit", at(i) + " uses feed " + mm(m.feed) + " mm/min; machine '" +
                                                    machine.name + "' allows (0, " + mm(machine.maxFeed) + "] mm/min.");
                rep.cutLength += len;
                if (m.feed > 0) rep.estimatedSeconds += len / m.feed * 60.0;
            }
            if (tool && over && frame.topZ - lowZ > tool->fluteLength + kEps)
                error("cam.sim.flute_length", at(i) + " cuts " + mm(frame.topZ - lowZ) +
                                                  " mm below the stock top but tool '" + tool->id +
                                                  "' has flute length " + mm(tool->fluteLength) + " mm.");
            if (tool) hm.cut(from, target, r, i);
            pos = target;
            break;
        }
        case MotionKind::ToolChange: {
            ++rep.toolChanges;
            rep.estimatedSeconds += machine.toolChangeSeconds;
            if (spindle) error("cam.sim.tool_change_unsafe", at(i) + " happens while the spindle is running.");
            if (!zKnown || pos.z < machine.safeZ - kEps)
                error("cam.sim.tool_change_unsafe", at(i) + " happens below safe Z " + mm(machine.safeZ) +
                                                        " mm or at an unknown height.");
            tool = tools.findByNumber(m.toolNumber);
            if (!tool)
                error("cam.sim.unknown_tool", at(i) + " selects T" + std::to_string(m.toolNumber) +
                                                  ", which is not in the tool library.");
            break;
        }
        case MotionKind::SpindleOn:
            if (!(m.rpm > 0) || m.rpm > machine.maxRpm + kEps)
                error("cam.sim.rpm_limit", at(i) + " requests " + mm(m.rpm) + " rpm; machine '" + machine.name +
                                               "' allows (0, " + mm(machine.maxRpm) + "] rpm.");
            else if (tool && m.rpm > tool->rpm + kEps)
                error("cam.sim.rpm_limit", at(i) + " requests " + mm(m.rpm) + " rpm, above the " + mm(tool->rpm) +
                                               " rpm set for tool '" + tool->id + "'.");
            spindle = true;
            break;
        case MotionKind::SpindleOff: spindle = false; break;
        case MotionKind::Dwell:
            if (m.seconds < 0) error("cam.sim.invalid_dwell", at(i) + " has a negative duration.");
            else rep.estimatedSeconds += m.seconds;
            break;
        }
    }
    if (spindle) error("cam.sim.spindle_on_at_end", "The program ends with the spindle running; add a spindle stop.");

    // ---- material verification -------------------------------------------------------
    std::vector<Target> targets;
    auto planR = planSetup(part, setup);
    if (planR) {
        for (const auto& p : planR.value().machinableNow) {
            const auto& op = part.operations[p.index];
            Target t;
            t.id = op.id.str();
            if (std::holds_alternative<domain::Hole>(op.shape)) {
                const HoleTarget h = holeTarget(part, frame, op);
                t.centre = h.centre;
                t.radius = h.radius;
                t.expectedFloor = h.floorZ;
                t.allowedFloor = h.through ? spoilLimit : h.floorZ;
            } else if (std::holds_alternative<domain::Groove>(op.shape)) {
                const GrooveTarget g = grooveTarget(part, frame, op);
                t.hole = false;
                t.start = g.start;
                t.length = geometry::length(g.end - g.start);
                t.dir = t.length > 0 ? (g.end - g.start) * (1.0 / t.length) : Vec2{1, 0};
                t.halfWidth = g.halfWidth;
                t.startOpen = g.startOpen;
                t.endOpen = g.endOpen;
                t.expectedFloor = t.allowedFloor = g.floorZ;
            } else {
                continue;
            }
            targets.push_back(std::move(t));
        }
    }
    const double tol = options.depthTolerance;
    for (const auto& t : targets) {
        double bx0, bx1, by0, by1;
        if (t.hole) {
            bx0 = t.centre.x - t.radius; bx1 = t.centre.x + t.radius;
            by0 = t.centre.y - t.radius; by1 = t.centre.y + t.radius;
        } else {
            const Vec2 e = t.start + t.dir * t.length;
            const double w = t.halfWidth;
            bx0 = std::min(t.start.x, e.x) - w; bx1 = std::max(t.start.x, e.x) + w;
            by0 = std::min(t.start.y, e.y) - w; by1 = std::max(t.start.y, e.y) + w;
        }
        int i0, i1, j0, j1;
        hm.range(bx0, bx1, true, i0, i1);
        hm.range(by0, by1, false, j0, j1);
        std::size_t n = 0, shallow = 0;
        double shallowest = -std::numeric_limits<double>::infinity();
        for (int j = j0; j <= j1; ++j)
            for (int i = i0; i <= i1; ++i) {
                if (!inExpected(t, hm.centre(i, j))) continue;
                ++n;
                const double z = std::min(hm.minZ(i, j), hm.topZ());
                if (z > t.expectedFloor + tol) {
                    ++shallow;
                    shallowest = std::max(shallowest, z);
                }
            }
        const char* kind = t.hole ? "Hole" : "Groove";
        if (n == 0) {
            rep.diagnostics.push_back({core::Severity::Warning, "cam.sim.unresolved",
                                       std::string(kind) + " '" + t.id + "' is too small for the simulation cell size " +
                                           mm(options.cellSize) + " mm and was not verified.", pid});
        } else if (shallow > 0) {
            std::string where = t.hole ? " (diameter " + mm(2 * t.radius) + " mm at machine X " + mm(t.centre.x) +
                                             ", Y " + mm(t.centre.y) + ")"
                                       : " (width " + mm(2 * t.halfWidth) + " mm from machine X " + mm(t.start.x) +
                                             ", Y " + mm(t.start.y) + ")";
            error(t.hole ? "cam.sim.hole_not_cut" : "cam.sim.groove_not_cut",
                  std::string(kind) + " '" + t.id + "'" + where + " is not cut to its floor Z " + mm(t.expectedFloor) +
                      " mm: " + std::to_string(shallow) + " of " + std::to_string(n) +
                      " sampled cells are shallower (shallowest at Z " + mm(shallowest) + " mm" +
                      (shallowest >= hm.topZ() - kEps ? ", i.e. untouched" : "") + ").");
        }
    }
    // Gouges and over-deep cuts.
    std::map<std::string, Agg> gouges, deep;
    const auto removed = hm.removedCells(kEps);
    rep.cellsRemoved = removed.size();
    for (const auto& [i, j] : removed) {
        const Vec2 c = hm.centre(i, j);
        const auto* cell = hm.cell(i, j);
        const Target* deepest = nullptr;
        for (const auto& t : targets)
            if (inAllowed(t, c) && (!deepest || t.allowedFloor < deepest->allowedFloor)) deepest = &t;
        if (!deepest) {
            gouges[program.motions[cell->motion].source.str()].add(c, cell->minZ, cell->motion);
        } else if (cell->minZ < deepest->allowedFloor - tol) {
            auto& a = deep[deepest->id];
            a.floor = deepest->allowedFloor;
            a.add(c, cell->minZ, cell->motion);
        }
    }
    for (const auto& [src, a] : gouges)
        error("cam.sim.gouge", "Material is removed outside every operation volume: " + std::to_string(a.count) +
                                   " cells between machine X " + mm(a.x0) + ".." + mm(a.x1) + ", Y " + mm(a.y0) + ".." +
                                   mm(a.y1) + " mm, down to Z " + mm(a.minZ) + " mm, by " +
                                   (src.empty() ? std::string("set-up motions") : "motions of operation '" + src + "'") +
                                   " (first: " + at(a.firstMotion) + ").");
    for (const auto& [id, a] : deep)
        error("cam.sim.too_deep", "Operation '" + id + "' is cut down to Z " + mm(a.minZ) + " mm, " +
                                      mm(a.floor - a.minZ) + " mm below its floor Z " + mm(a.floor) + " mm (" +
                                      std::to_string(a.count) + " cells; first: " + at(a.firstMotion) + ").");
    return rep;
}

} // namespace fos::cam
