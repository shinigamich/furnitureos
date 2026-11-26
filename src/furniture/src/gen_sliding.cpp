// SPDX-License-Identifier: AGPL-3.0-only
/// Sliding doors: door parts in the two track lanes, track / roller jobs, recessed grips.
#include "gen_context.hpp"

#include <algorithm>
#include <cmath>

namespace fos::furniture::detail {
namespace {

/// Door count of `spec`: the expression (whole number) or the fixed count, checked against
/// kMinSlidingDoors..kMaxSlidingDoors.
core::Result<int> doorCount(const Gen& g, const SlidingDoorSpec& spec, const std::string& label) {
    double value = spec.count;
    if (!spec.countExpression.empty()) {
        auto r = g.def.parameters.evaluate(spec.countExpression);
        if (!r)
            return core::makeError(core::ErrorCode::InvalidArgument,
                                   label + ": the sliding door count expression '" + spec.countExpression +
                                       "' cannot be evaluated: " + r.error().message,
                                   g.def.id.str());
        value = r.value();
        if (!std::isfinite(value) || std::fabs(value - std::round(value)) > 1e-6)
            return core::makeError(core::ErrorCode::InvalidArgument,
                                   label + ": the sliding door count expression '" + spec.countExpression +
                                       "' gives " + num(value) + ", not a whole number of doors.",
                                   g.def.id.str());
        value = std::round(value);
    }
    if (value < kMinSlidingDoors || value > kMaxSlidingDoors)
        return core::makeError(core::ErrorCode::InvalidArgument,
                               label + " has " + num(value) + " sliding doors; use " +
                                   std::to_string(kMinSlidingDoors) + " to " + std::to_string(kMaxSlidingDoors) + ".",
                               g.def.id.str());
    return static_cast<int>(value);
}

/// RECESSED GRIP RULE: one rectangular pocket on the visible face B, centred on the door
/// width; Center = at mid height, Top / Bottom = its centre handle_end_offset from that edge.
/// Size from the optional parameters sliding_grip_length (vertical, 200), sliding_grip_width
/// (40) and sliding_grip_depth (8 mm).
/// CORNER RADIUS: the pocket's corners are rounded with kGripCornerRadius = 6 mm, a GENERIC value
/// (not from a grip or tool datasheet) chosen so any end mill of 12 mm diameter or smaller can cut
/// it; it is capped at half the grip's narrower side (a narrower grip gets fully rounded ends).
constexpr double kGripCornerRadius = 6.0;

core::Result<void> addGrip(Gen& g, std::size_t partIndex, HandlePosition pos) {
    namespace p = param;
    domain::Part& door = g.assembly.parts[partIndex];
    const double len = g.opt("sliding_grip_length", 200), wid = g.opt("sliding_grip_width", 40),
                 dep = g.opt("sliding_grip_depth", 8);
    const std::string who = "Recessed grip of '" + door.name + "' (" + door.id.str() + ")";
    if (!(len > 0) || !(wid > 0) || !(dep > 0))
        return core::makeError(core::ErrorCode::ConstraintViolation,
                               who + ": sliding_grip_length / _width / _depth must be positive (are " + num(len) + ", " +
                                   num(wid) + ", " + num(dep) + " mm).",
                               door.id.str());
    if (dep >= door.thickness - 2 - core::kLengthEps)
        return core::makeError(core::ErrorCode::ConstraintViolation,
                               who + " is " + num(dep) + " mm deep; the " + num(door.thickness) +
                                   " mm door needs more than 2 mm left behind it.",
                               door.id.str());
    const geometry::Aabb b = door.bounds();
    const double cx = (b.min.x + b.max.x) / 2, off = g.v(p::kHandleEndOffset);
    double cz = (b.min.z + b.max.z) / 2;
    if (pos == HandlePosition::Top) cz = b.max.z - off;
    if (pos == HandlePosition::Bottom) cz = b.min.z + off;
    if (wid > b.max.x - b.min.x - 10 || cz - len / 2 < b.min.z + 5 - core::kLengthEps ||
        cz + len / 2 > b.max.z - 5 + core::kLengthEps)
        return core::makeError(core::ErrorCode::ConstraintViolation,
                               who + " (" + num(wid) + " x " + num(len) + " mm, centre " + num(cz - b.min.z) +
                                   " mm above the door bottom) does not fit on the door with 5 mm of material at "
                                   "every edge.",
                               door.id.str());
    domain::Pocket pocket;
    pocket.face = domain::Face::B;
    pocket.depth = dep;
    pocket.cornerRadius = std::min(kGripCornerRadius, std::min(wid, len) / 2);
    for (const auto& [dx, dz] : {std::pair{-1, -1}, {1, -1}, {1, 1}, {-1, 1}}) {
        const auto c = machining::assemblyToFace(door, domain::Face::B,
                                                 {cx + dx * wid / 2, b.min.y, cz + dz * len / 2});
        pocket.contour.push_back({c.u, c.v});
    }
    domain::Operation op;
    op.id = domain::OperationId(door.id.str() + "/op.grip");
    op.shape = pocket;
    op.source = g.partId("rule.sliding_grip");
    op.note = "Recessed grip";
    door.operations.push_back(std::move(op));
    return {};
}

} // namespace

core::Result<double> buildSlidingDoors(Gen& g, const FrontSpec& spec, const std::string& path, const Box& box,
                                       const Bounds& b) {
    namespace p = param;
    const std::string label = "Section '" + sectionLabel(path) + "' of cabinet '" + g.def.id.str() + "'";
    auto count = doorCount(g, spec.sliding, label);
    if (!count) return count.error();
    if (b.top.size() != 1 || b.bottom.size() != 1)
        return core::makeError(core::ErrorCode::ConstraintViolation,
                               label + ": sliding door tracks are screwed under the panel above and onto the panel "
                               "below the opening, but the section is closed by " + std::to_string(b.top.size()) +
                                   " panel(s) above; use a full top panel.",
                               g.def.id.str());
    const domain::HardwareId& sysId = g.def.hardware.slidingSystem;
    const domain::HardwareDefinition* sys = g.libs.findHardware(sysId);
    if (!sys)
        return core::makeError(core::ErrorCode::NotFound,
                               label + ": the sliding door system '" + sysId.str() +
                                   "' is not in the hardware library.",
                               g.def.id.str());
    const double ft = g.v(p::kFrontThickness);
    auto plan = hardware::planSlidingDoors(*sys, {box.x0, box.x1, box.z0, box.z1, g.v(p::kInteriorDepth)},
                                           count.value(), ft);
    if (!plan)
        return core::makeError(plan.error().code, label + ": " + plan.error().message, g.def.id.str());
    const auto& pl = plan.value();

    // Tracks first (top, bottom), then per door its parts, rollers and grip.
    for (const bool top : {true, false}) {
        Job j;
        j.kind = JobKind::SlidingTrack;
        j.instance = joinSlot({"hw.track", path, top ? "top" : "bottom"});
        j.a = top ? b.top[0] : b.bottom[0];
        j.sharedPanel = top ? !b.topOuter : !b.bottomOuter;
        j.track.top = top;
        j.track.x0 = pl.trackX0;
        j.track.x1 = pl.trackX1;
        j.track.y = (pl.trackY0 + pl.trackY1) / 2;
        g.jobs.push_back(std::move(j));
    }
    const Edges all{EdgeUse::Front, EdgeUse::Front, EdgeUse::Front, EdgeUse::Front};
    for (std::size_t i = 0; i < pl.doors.size(); ++i) {
        const auto& d = pl.doors[i];
        const std::string slot = joinSlot({"sliding", path, std::to_string(i + 1)});
        // Like every door: length -> +Z, width -> +X, thickness -> +Y; face B (visible) at
        // y = yFront, face A (inner) at y = yBack.
        const std::size_t part =
            g.addPart(slot, "Sliding door " + std::to_string(i + 1), domain::PartRole::Door, MaterialRole::Front,
                      d.z1 - d.z0, d.x1 - d.x0, ft, Transform::fromAxes(kUp, kRight, {d.x0, d.yFront, d.z0}), all);
        g.assembly.parts[part].custom["furniture.sliding_plane"] = d.plane == 0 ? "front" : "back";
        Job j;
        j.kind = JobKind::SlidingRollers;
        j.instance = "hw.rollers." + slot;
        j.a = part;
        g.jobs.push_back(std::move(j));
        if (spec.handle != HandlePosition::None)
            if (auto r = addGrip(g, part, spec.handle); !r) return r.error();
    }
    return pl.interiorFrontY;
}

} // namespace fos::furniture::detail
