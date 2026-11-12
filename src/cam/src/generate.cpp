// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/cam/generate.hpp"

#include "fos/core/units.hpp"
#include "fos/machining/ordering.hpp"
#include "fos/machining/validation.hpp"

#include <algorithm>
#include <cmath>

namespace fos::cam {

using core::ErrorCode;
using core::makeError;
using geometry::Vec2;
using geometry::Vec3;

namespace {

std::string mm(double v) { return core::formatLength(v, 3); }

/// Appends motions while tracking position and machine state.
class Builder {
public:
    Builder(Program& p, const CamOptions& o) : prog_(p), opt_(o) {}

    void start() {
        push(Motion::rapidZ(prog_.safeZ));
        pos_.z = prog_.safeZ;
    }
    void useTool(const Tool& t) {
        if (tool_ == t.number) return;
        if (spindle_) { push(Motion::spindleOff()); spindle_ = false; }
        if (pos_.z < prog_.safeZ) rapid({pos_.x, pos_.y, prog_.safeZ}, {});
        push(Motion::toolChange(t.number));
        push(Motion::spindleOn(t.rpm));
        if (opt_.spindleSpinUpDwell > 0) push(Motion::dwell(opt_.spindleSpinUpDwell));
        tool_ = t.number;
        spindle_ = true;
    }
    /// XY travel at the current height (>= clearance plane), then straight down to the clearance plane.
    void moveAbove(Vec2 xy, const domain::OperationId& src) {
        if (!xyKnown_ || !core::approxEqual(xy.x, pos_.x) || !core::approxEqual(xy.y, pos_.y))
            rapid({xy.x, xy.y, pos_.z}, src);
        if (pos_.z > prog_.clearanceZ) rapid({xy.x, xy.y, prog_.clearanceZ}, src);
    }
    void rapid(Vec3 p, const domain::OperationId& src) {
        push(Motion::rapid(p, src));
        pos_ = p;
        xyKnown_ = true;
    }
    void linear(Vec3 p, double feed, const domain::OperationId& src) {
        push(Motion::linear(p, feed, src));
        pos_ = p;
    }
    void dwell(double s, const domain::OperationId& src) { push(Motion::dwell(s, src)); }
    void retract(const domain::OperationId& src) { rapid({pos_.x, pos_.y, prog_.clearanceZ}, src); }
    void finish() {
        if (xyKnown_ && pos_.z < prog_.safeZ) rapid({pos_.x, pos_.y, prog_.safeZ}, {});
        if (spindle_) { push(Motion::spindleOff()); spindle_ = false; }
    }

private:
    void push(Motion m) { prog_.motions.push_back(std::move(m)); }
    Program& prog_;
    const CamOptions& opt_;
    Vec3 pos_;
    bool xyKnown_ = false;
    int tool_ = 0;
    bool spindle_ = false;
};

core::Result<void> checkToolLimits(const Tool& t, const MachineProfile& m) {
    const std::string who = std::string(toString(t.kind)) + " '" + t.id + "' (T" + std::to_string(t.number) + ")";
    if (t.rpm > m.maxRpm)
        return makeError(ErrorCode::ConstraintViolation, who + " runs at " + mm(t.rpm) + " rpm but machine '" + m.name +
                                                             "' allows at most " + mm(m.maxRpm) + " rpm.", t.id);
    const double f = std::max(t.feedRate, t.plungeRate);
    if (f > m.maxFeed)
        return makeError(ErrorCode::ConstraintViolation, who + " uses a feed of " + mm(f) + " mm/min but machine '" +
                                                             m.name + "' allows at most " + mm(m.maxFeed) + " mm/min.",
                         t.id);
    return {};
}

void drill(Builder& b, const HoleTarget& h, double floorZ, const Tool& t, const CamOptions& o, double topZ) {
    b.moveAbove(h.centre, h.id);
    std::vector<double> levels;
    const double depth = topZ - floorZ;
    if (o.peckDepth > 0) {
        for (int k = 1;; ++k) {
            const double d = k * o.peckDepth;
            if (d >= depth - core::kLengthEps) break;
            levels.push_back(topZ - d);
        }
    }
    levels.push_back(floorZ);
    for (std::size_t i = 0; i < levels.size(); ++i) {
        b.linear({h.centre.x, h.centre.y, levels[i]}, t.plungeRate, h.id);
        if (i + 1 == levels.size() && o.dwellAtHoleBottom > 0) b.dwell(o.dwellAtHoleBottom, h.id);
        b.retract(h.id);
    }
}

core::Result<void> groove(Builder& b, const GrooveTarget& g, const Tool& t, const CamOptions& o,
                          const domain::Part& part, core::Diagnostics& diags) {
    const double r = t.diameter / 2;
    const double len = geometry::length(g.end - g.start);
    const Vec2 dir = (g.end - g.start) * (1.0 / len);
    const Vec2 nrm{-dir.y, dir.x};
    const double closedNeeded = (g.startOpen ? 0 : r) + (g.endOpen ? 0 : r);
    if (len < closedNeeded - core::kLengthEps)
        return makeError(ErrorCode::ConstraintViolation,
                         "Groove '" + g.id.str() + "' of '" + part.name + "' is " + mm(len) +
                             " mm long, too short for end mill '" + t.id + "' (" + mm(t.diameter) +
                             " mm) at its closed end(s).", part.id.str());
    for (bool open : {g.startOpen, g.endOpen})
        if (!open)
            diags.push_back({core::Severity::Warning, "cam.groove.rounded_end",
                             "Groove '" + g.id.str() + "' of '" + part.name +
                                 "' ends inside the panel; the end mill leaves that end rounded with radius " +
                                 mm(r) + " mm (the corners are not cut).", part.id.str()});
    const Vec2 s = g.start + dir * (g.startOpen ? -(r + o.grooveLeadOut) : r);
    const Vec2 e = g.end + dir * (g.endOpen ? (r + o.grooveLeadOut) : -r);
    const double span = std::max(0.0, 2 * g.halfWidth - t.diameter);
    int passes = 1;
    if (span > core::kLengthEps) passes = static_cast<int>(std::ceil(span / (o.stepoverFraction * t.diameter) - 1e-9)) + 1;
    const double depth = g.topZ - g.floorZ;
    std::vector<double> levels;
    for (int k = 1;; ++k) {
        const double d = k * t.maxDepthOfCut;
        if (d >= depth - core::kLengthEps) break;
        levels.push_back(g.topZ - d);
    }
    levels.push_back(g.floorZ);
    for (double z : levels) {
        for (int i = 0; i < passes; ++i) {
            const double off = passes == 1 ? 0.0 : -span / 2 + span * i / (passes - 1);
            const Vec2 p0 = s + nrm * off, p1 = e + nrm * off;
            b.moveAbove(p0, g.id);
            b.linear({p0.x, p0.y, z}, t.plungeRate, g.id);
            b.linear({p1.x, p1.y, z}, t.feedRate, g.id);
            b.retract(g.id);
        }
    }
    return {};
}

} // namespace

core::Result<CamResult> generateProgram(const domain::Part& part, const Setup& setup, const MachineProfile& machine,
                                        const ToolLibrary& tools, const CamOptions& o) {
    auto frameR = makeSetupFrame(part, setup);
    if (!frameR) return frameR.error();
    const SetupFrame& frame = frameR.value();
    CamResult res;
    for (auto& d : validateProfile(machine)) {
        if (d.severity == core::Severity::Error) return makeError(ErrorCode::InvalidArgument, d.message, machine.name);
        res.diagnostics.push_back(std::move(d));
    }
    if (!(o.stepoverFraction > 0 && o.stepoverFraction <= 1) || o.peckDepth < 0 || o.breakthroughAllowance < 0 ||
        o.dwellAtHoleBottom < 0 || o.spindleSpinUpDwell < 0 || o.grooveLeadOut < 0)
        return makeError(ErrorCode::InvalidArgument,
                         "CAM options are invalid: stepoverFraction must be in (0, 1] and peck depth, breakthrough "
                         "allowance, dwells and lead-out must not be negative.");
    const auto vd = machining::validateOperations(part);
    std::size_t nErr = 0;
    const core::Diagnostic* first = nullptr;
    for (const auto& d : vd)
        if (d.severity == core::Severity::Error) { ++nErr; if (!first) first = &d; }
    if (first)
        return makeError(ErrorCode::ConstraintViolation,
                         "Part '" + part.name + "' has " + std::to_string(nErr) +
                             " machining validation error(s); no toolpath is generated. First: " + first->message,
                         part.id.str());
    auto planR = planSetup(part, setup);
    if (!planR) return planR.error();
    res.plan = std::move(planR).value();
    for (const auto& p : res.plan.needsOtherSetup)
        res.diagnostics.push_back({core::Severity::Warning, "cam.setup.needs_other_setup", p.reason, part.id.str()});
    for (const auto& p : res.plan.unsupported)
        res.diagnostics.push_back({core::Severity::Warning, "cam.setup.unsupported", p.reason, part.id.str()});

    Program& prog = res.program;
    prog.partId = part.id.str();
    prog.partName = part.name;
    prog.faceUp = setup.faceUp;
    prog.safeZ = machine.safeZ;
    prog.clearanceZ = frame.topZ + machine.clearance;
    if (machine.safeZ < prog.clearanceZ - core::kLengthEps)
        return makeError(ErrorCode::ConstraintViolation,
                         "Safe Z " + mm(machine.safeZ) + " mm of machine '" + machine.name +
                             "' is below the clearance plane Z " + mm(prog.clearanceZ) + " mm for '" + part.name +
                             "' (stock top " + mm(frame.topZ) + " mm + clearance " + mm(machine.clearance) + " mm).",
                         part.id.str());

    std::vector<bool> now(part.operations.size(), false);
    for (const auto& p : res.plan.machinableNow) now[p.index] = true;

    Builder b(prog, o);
    b.start();
    const auto order = machining::orderOperations(part);
    for (const auto& group : order.groups) {
        if (group.face != setup.faceUp || group.tool == machining::ToolKind::PocketMill) continue;
        for (std::size_t idx : group.order) {
            if (!now[idx]) continue;
            const auto& op = part.operations[idx];
            if (const auto* h = std::get_if<domain::Hole>(&op.shape)) {
                const HoleTarget t = holeTarget(part, frame, op);
                double floorZ = t.floorZ;
                if (h->through) {
                    if (o.breakthroughAllowance > machine.maxSpoilboardPenetration + core::kLengthEps)
                        return makeError(ErrorCode::ConstraintViolation,
                                         "Through hole '" + op.id.str() + "' of '" + part.name +
                                             "': breakthrough allowance " + mm(o.breakthroughAllowance) +
                                             " mm exceeds the spoilboard limit " +
                                             mm(machine.maxSpoilboardPenetration) + " mm of machine '" + machine.name +
                                             "'.", part.id.str());
                    floorZ = frame.bottomZ - o.breakthroughAllowance;
                }
                auto tool = tools.selectDrill(h->diameter, frame.topZ - floorZ);
                if (!tool)
                    return makeError(tool.error().code, "Hole '" + op.id.str() + "' of '" + part.name + "': " +
                                                            tool.error().message, part.id.str());
                if (auto lim = checkToolLimits(*tool.value(), machine); !lim) return lim.error();
                b.useTool(*tool.value());
                drill(b, t, floorZ, *tool.value(), o, frame.topZ);
                if (std::find(res.toolsUsed.begin(), res.toolsUsed.end(), tool.value()->number) == res.toolsUsed.end())
                    res.toolsUsed.push_back(tool.value()->number);
            } else if (const auto* g = std::get_if<domain::Groove>(&op.shape)) {
                auto tool = tools.selectGrooveCutter(g->width, g->depth);
                if (!tool)
                    return makeError(tool.error().code, "Groove '" + op.id.str() + "' of '" + part.name + "': " +
                                                            tool.error().message, part.id.str());
                if (auto lim = checkToolLimits(*tool.value(), machine); !lim) return lim.error();
                b.useTool(*tool.value());
                if (auto r = groove(b, grooveTarget(part, frame, op), *tool.value(), o, part, res.diagnostics); !r)
                    return r.error();
                if (std::find(res.toolsUsed.begin(), res.toolsUsed.end(), tool.value()->number) == res.toolsUsed.end())
                    res.toolsUsed.push_back(tool.value()->number);
            }
        }
    }
    b.finish();
    return res;
}

} // namespace fos::cam
