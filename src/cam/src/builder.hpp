// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
// Internal: motion builder shared by the hole/groove generator and the contour router.
#include "fos/cam/generate.hpp"
#include "fos/core/units.hpp"

#include <algorithm>
#include <string>

namespace fos::cam::detail {

using geometry::Vec2;
using geometry::Vec3;

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
    /// Circular move at feed from the current position to p around centre (exact).
    void arc(Vec3 p, Vec2 centre, bool ccw, double feed, const domain::OperationId& src) {
        push(Motion::arc(p, centre, ccw, feed, src));
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

/// Tool rpm / feeds against the machine limits.
inline core::Result<void> checkToolLimits(const Tool& t, const MachineProfile& m) {
    const std::string who = std::string(toString(t.kind)) + " '" + t.id + "' (T" + std::to_string(t.number) + ")";
    if (t.rpm > m.maxRpm)
        return core::makeError(core::ErrorCode::ConstraintViolation, who + " runs at " + core::formatLength(t.rpm, 3) + " rpm but machine '" + m.name +
                                                             "' allows at most " + core::formatLength(m.maxRpm, 3) + " rpm.", t.id);
    const double f = std::max(t.feedRate, t.plungeRate);
    if (f > m.maxFeed)
        return core::makeError(core::ErrorCode::ConstraintViolation, who + " uses a feed of " + core::formatLength(f, 3) + " mm/min but machine '" +
                                                             m.name + "' allows at most " + core::formatLength(m.maxFeed, 3) + " mm/min.",
                         t.id);
    return {};
}

} // namespace fos::cam::detail
