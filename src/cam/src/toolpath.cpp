// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/cam/toolpath.hpp"

#include "fos/core/units.hpp"

namespace fos::cam {

const char* toString(MotionKind k) noexcept {
    switch (k) {
    case MotionKind::Rapid: return "Rapid";
    case MotionKind::Linear: return "Linear";
    case MotionKind::ToolChange: return "ToolChange";
    case MotionKind::SpindleOn: return "SpindleOn";
    case MotionKind::SpindleOff: return "SpindleOff";
    case MotionKind::Dwell: return "Dwell";
    }
    return "Unknown";
}

Motion Motion::rapid(geometry::Vec3 p, domain::OperationId src) {
    Motion m;
    m.kind = MotionKind::Rapid;
    m.target = p;
    m.source = std::move(src);
    return m;
}
Motion Motion::rapidZ(double z) {
    Motion m;
    m.kind = MotionKind::Rapid;
    m.target = {0, 0, z};
    m.zOnly = true;
    return m;
}
Motion Motion::linear(geometry::Vec3 p, double feed, domain::OperationId src) {
    Motion m;
    m.kind = MotionKind::Linear;
    m.target = p;
    m.feed = feed;
    m.source = std::move(src);
    return m;
}
Motion Motion::toolChange(int number) {
    Motion m;
    m.kind = MotionKind::ToolChange;
    m.toolNumber = number;
    return m;
}
Motion Motion::spindleOn(double rpm) {
    Motion m;
    m.kind = MotionKind::SpindleOn;
    m.rpm = rpm;
    return m;
}
Motion Motion::spindleOff() {
    Motion m;
    m.kind = MotionKind::SpindleOff;
    return m;
}
Motion Motion::dwell(double seconds, domain::OperationId src) {
    Motion m;
    m.kind = MotionKind::Dwell;
    m.seconds = seconds;
    m.source = std::move(src);
    return m;
}

std::string describe(const Motion& m) {
    auto f = [](double v) { return core::formatLength(v, 3); };
    auto pt = [&](geometry::Vec3 p) { return "(" + f(p.x) + ", " + f(p.y) + ", " + f(p.z) + ")"; };
    switch (m.kind) {
    case MotionKind::Rapid: return m.zOnly ? "Rapid to Z " + f(m.target.z) : "Rapid to " + pt(m.target);
    case MotionKind::Linear: return "Linear to " + pt(m.target) + " F" + f(m.feed);
    case MotionKind::ToolChange: return "Tool change to T" + std::to_string(m.toolNumber);
    case MotionKind::SpindleOn: return "Spindle on S" + f(m.rpm);
    case MotionKind::SpindleOff: return "Spindle off";
    case MotionKind::Dwell: return "Dwell " + f(m.seconds) + " s";
    }
    return "Unknown motion";
}

} // namespace fos::cam
