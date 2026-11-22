// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/hardware/sliding_doors.hpp"

#include "fos/hardware/catalog.hpp"
#include "util.hpp"

#include <cmath>
#include <map>

namespace fos::hardware {
using namespace detail;
using domain::Face;
using domain::Part;
using geometry::Vec3;

namespace {

core::Result<std::map<std::string, double>> readParams(const domain::HardwareDefinition& sys) {
    if (sys.kind != domain::HardwareKind::SlidingSystem)
        return core::makeError(core::ErrorCode::InvalidArgument,
                               "Hardware definition '" + sys.id.str() + "' (" + sys.name + ") is of kind " +
                                   domain::toString(sys.kind) + " but a sliding door system is required.",
                               sys.id.str());
    std::map<std::string, double> v;
    for (const auto& key : requiredParams(sys.kind)) {
        auto r = requireParam(sys, key);
        if (!r) return r.error();
        v[key] = r.value();
    }
    return v;
}

std::string nameOf(const domain::HardwareDefinition& sys) { return sys.name.empty() ? sys.id.str() : sys.name; }

core::Error violation(std::string message, const std::string& entity) {
    return core::Error{core::ErrorCode::ConstraintViolation, std::move(message), entity};
}

/// The face of `p` whose outward normal is `dir`; std::nullopt when neither A nor B is.
std::optional<Face> faceTowards(const Part& p, Vec3 dir) {
    for (const Face f : {Face::A, Face::B})
        if (dot(outwardNormal(p, f), dir) > 1 - kAngleEps) return f;
    return std::nullopt;
}

} // namespace

core::Result<SlidingDoorPlan> planSlidingDoors(const domain::HardwareDefinition& sys, const SlidingOpening& o,
                                               int doorCount, double doorThickness) {
    auto params = readParams(sys);
    if (!params) return params.error();
    auto& v = params.value();
    using namespace keys;
    const std::string what = "sliding door system '" + nameOf(sys) + "'";
    const std::string id = sys.id.str();
    if (v[kDoorBottomClearance] >= v[kDoorHeightDeduction] - core::kLengthEps)
        return core::makeError(core::ErrorCode::InvalidArgument,
                               "Parameter door_bottom_clearance (" + num(v[kDoorBottomClearance]) + " mm) of " + what +
                                   " must be smaller than door_height_deduction (" + num(v[kDoorHeightDeduction]) +
                                   " mm).",
                               id);
    if (doorCount < 2)
        return violation("A " + what + " needs at least 2 doors; " + std::to_string(doorCount) + " requested.", id);
    if (!(doorThickness > 0))
        return violation("The door thickness for " + what + " must be positive (is " + num(doorThickness) + " mm).", id);
    const double width = o.x1 - o.x0, height = o.z1 - o.z0;
    if (!(width > 0) || !(height > 0) || !(o.depth > 0))
        return violation("The opening for " + what + " measures " + num(width) + " x " + num(height) + " x " +
                             num(o.depth) + " mm; all three must be positive.",
                         id);

    // Depth: tracks, then the interior behind them.
    const double reserve = v[kTrackSetback] + v[kTrackDepth] + v[kInteriorClearance];
    if (o.depth < reserve - core::kLengthEps)
        return violation("The cabinet is too shallow for the tracks of " + what + ": the interior is " + num(o.depth) +
                             " mm deep but the tracks need " + num(v[kTrackSetback]) + " (setback) + " +
                             num(v[kTrackDepth]) + " (track depth) + " + num(v[kInteriorClearance]) +
                             " (clearance) = " + num(reserve) + " mm.",
                         id);
    if (o.depth - reserve < v[kInteriorDepthMin] - core::kLengthEps)
        return violation("The cabinet is too shallow for " + what + ": behind the tracks (" + num(reserve) +
                             " mm) only " + num(o.depth - reserve) + " mm of the " + num(o.depth) +
                             " mm interior depth remain; at least " + num(v[kInteriorDepthMin]) + " mm are needed (depth " +
                             num(reserve + v[kInteriorDepthMin]) + " mm or more).",
                         id);
    // Lanes.
    if (doorThickness >= v[kPlanePitch] - core::kLengthEps)
        return violation("Doors of " + num(doorThickness) + " mm are too thick for " + what + ": the two track lanes are " +
                             num(v[kPlanePitch]) + " mm apart, so neighbouring doors would touch.",
                         id);
    if (v[kPlaneFrontOffset] + v[kPlanePitch] + doorThickness > v[kTrackDepth] + core::kLengthEps)
        return violation("Doors of " + num(doorThickness) + " mm are too thick for " + what + ": the back lane needs " +
                             num(v[kPlaneFrontOffset]) + " + " + num(v[kPlanePitch]) + " + " + num(doorThickness) +
                             " = " + num(v[kPlaneFrontOffset] + v[kPlanePitch] + doorThickness) +
                             " mm but the track is " + num(v[kTrackDepth]) + " mm deep.",
                         id);
    // Widths: n doors overlap n - 1 times.
    const double n = doorCount, overlap = v[kDoorOverlap];
    const double w = (width + (n - 1) * overlap) / n;
    if (w < v[kDoorWidthMin] - core::kLengthEps || w <= 2 * overlap + core::kLengthEps)
        return violation("The doors are too narrow for " + what + ": " + std::to_string(doorCount) + " doors in a " +
                             num(width) + " mm opening with " + num(overlap) + " mm overlap are " + num(w) +
                             " mm wide; the system needs at least " +
                             num(std::max(v[kDoorWidthMin], 2 * overlap)) + " mm. Use fewer doors.",
                         id);
    if (w > v[kDoorWidthMax] + core::kLengthEps)
        return violation("The doors are too wide for " + what + ": " + std::to_string(doorCount) + " doors in a " +
                             num(width) + " mm opening are " + num(w) + " mm wide; the system allows at most " +
                             num(v[kDoorWidthMax]) + " mm. Use more doors.",
                         id);
    const double h = height - v[kDoorHeightDeduction];
    if (h <= core::kLengthEps)
        return violation("The opening of " + num(height) + " mm is too low for " + what + ": the track system takes " +
                             num(v[kDoorHeightDeduction]) + " mm of the door height.",
                         id);

    SlidingDoorPlan plan;
    plan.doorWidth = w;
    plan.doorHeight = h;
    plan.overlap = overlap;
    plan.trackX0 = o.x0;
    plan.trackX1 = o.x1;
    plan.trackY0 = v[kTrackSetback];
    plan.trackY1 = v[kTrackSetback] + v[kTrackDepth];
    plan.interiorFrontY = reserve;
    for (int i = 0; i < doorCount; ++i) {
        SlidingDoorSlot d;
        d.plane = i % 2 == 0 ? 1 : 0;
        d.x0 = o.x0 + i * (w - overlap);
        d.x1 = d.x0 + w;
        d.z0 = o.z0 + v[kDoorBottomClearance];
        d.z1 = d.z0 + h;
        d.yFront = plan.trackY0 + v[kPlaneFrontOffset] + (d.plane == 1 ? v[kPlanePitch] : 0.0);
        d.yBack = d.yFront + doorThickness;
        plan.doors.push_back(d);
    }
    plan.doors.back().x1 = o.x1; // exact (no rounding drift at the right side)
    return plan;
}

int slidingTrackScrewCount(double length, double e, double p) noexcept {
    if (!(p > 0) || length < 2 * e - core::kLengthEps) return 0;
    const double span = std::max(0.0, length - 2 * e);
    return static_cast<int>(std::ceil(span / p - 1e-9)) + 1;
}

core::Result<Placement> placeSlidingTrack(const Part& panel, const domain::HardwareDefinition& sys,
                                          const SlidingTrackConfig& cfg) {
    if (auto r = checkInstanceId(cfg.instanceId); !r) return r.error();
    if (auto r = checkPart(panel); !r) return r.error();
    auto params = readParams(sys);
    if (!params) return params.error();
    auto& v = params.value();
    using namespace keys;
    const std::string what = nameOf(sys);
    const auto face = faceTowards(panel, cfg.top ? Vec3{0, 0, -1} : Vec3{0, 0, 1});
    if (!face)
        return violation("Panel " + quoted(panel) + " must be horizontal to carry the " +
                             (cfg.top ? "top" : "bottom") + " track of '" + what + "'.",
                         panel.id.str());
    const double length = cfg.x1 - cfg.x0, e = v[kTrackScrewEndDistance];
    const int count = slidingTrackScrewCount(length, e, v[kTrackScrewPitch]);
    if (count < 2)
        return violation("The " + std::string(cfg.top ? "top" : "bottom") + " track of '" + what + "' would be " +
                             num(length) + " mm long, shorter than two screw end distances (2 x " + num(e) + " mm).",
                         panel.id.str());
    const double depth = cfg.maxHoleDepth > 0 ? std::min(cfg.maxHoleDepth, v[kTrackScrewDepth]) : v[kTrackScrewDepth];
    if (depth >= panel.thickness - cfg.fit.minRemainingMaterial - core::kLengthEps)
        return violation("The " + num(depth) + " mm deep track screw holes of '" + what + "' would leave " +
                             num(panel.thickness - depth) + " mm of the " + num(panel.thickness) + " mm thick " +
                             quoted(panel) + "; more than " + num(cfg.fit.minRemainingMaterial) + " mm must remain.",
                         panel.id.str());
    const machining::Range z = extentAlong(panel, {0, 0, 1});
    const double zFace = cfg.top ? z.min : z.max;
    const double step = (length - 2 * e) / (count - 1);

    OpWriter w(cfg.instanceId);
    domain::HardwareInstance inst;
    inst.id = cfg.instanceId;
    inst.definition = sys.id;
    inst.quantity = 1;
    inst.parts = {panel.id};
    inst.note = std::string(cfg.top ? "Top" : "Bottom") + " track " + num(length) + " mm (cut to the opening width) on " +
                panel.name;
    for (int k = 0; k < count; ++k) {
        const Vec3 pt{cfg.x0 + e + k * step, cfg.y, zFace};
        if (k == 0) inst.position = pt;
        const auto c = machining::assemblyToFace(panel, *face, pt);
        if (!circleOnFace(panel, *face, c.u, c.v, v[kTrackScrewDiameter] / 2))
            return violation("Track screw hole at (" + num(c.u) + ", " + num(c.v) + ") on face " +
                                 domain::toString(*face) + " of " + quoted(panel) + " would not lie on the panel (" +
                                 num(panel.length) + " x " + num(panel.width) + " mm).",
                             panel.id.str());
        w.add(panel, domain::Hole{*face, c.u, c.v, v[kTrackScrewDiameter], depth, false},
              what + ": " + (cfg.top ? "top" : "bottom") + " track screw " + std::to_string(k + 1) + " of " +
                  std::to_string(count));
    }
    Placement out;
    out.instances = {inst};
    out.operations = w.take();
    return out;
}

core::Result<Placement> placeSlidingDoorRollers(const Part& door, const domain::HardwareDefinition& sys,
                                                const SlidingRollerConfig& cfg) {
    if (auto r = checkInstanceId(cfg.instanceId); !r) return r.error();
    if (auto r = checkPart(door); !r) return r.error();
    auto params = readParams(sys);
    if (!params) return params.error();
    auto& v = params.value();
    using namespace keys;
    const std::string what = nameOf(sys);
    const auto face = faceTowards(door, {0, 1, 0});
    if (!face)
        return violation("Door " + quoted(door) + " must stand vertically with a face towards the cabinet (+y) to take "
                         "the rollers of '" + what + "'.",
                         door.id.str());
    if (v[kRollerHoleDepth] >= door.thickness - cfg.fit.minRemainingMaterial - core::kLengthEps)
        return violation("The " + num(v[kRollerHoleDepth]) + " mm deep roller holes of '" + what + "' would leave " +
                             num(door.thickness - v[kRollerHoleDepth]) + " mm of the " + num(door.thickness) +
                             " mm thick door " + quoted(door) + "; more than " + num(cfg.fit.minRemainingMaterial) +
                             " mm must remain.",
                         door.id.str());
    const machining::Range x = extentAlong(door, {1, 0, 0}), z = extentAlong(door, {0, 0, 1});
    const double y = extentAlong(door, {0, 1, 0}).max;
    const double side = v[kRollerHoleFromSideEdge];
    const double zs[2] = {z.min + v[kRollerHoleFromBottomEdge], z.max - v[kGuideHoleFromTopEdge]};

    OpWriter w(cfg.instanceId);
    domain::HardwareInstance inst;
    inst.id = cfg.instanceId;
    inst.definition = sys.id;
    inst.quantity = 1;
    inst.parts = {door.id};
    inst.note = "Rollers and top guides of " + door.name;
    for (int row = 0; row < 2; ++row) {
        for (int k = 0; k < 2; ++k) {
            const Vec3 pt{k == 0 ? x.min + side : x.max - side, y, zs[row]};
            if (row == 0 && k == 0) inst.position = pt;
            const auto c = machining::assemblyToFace(door, *face, pt);
            if (!circleOnFace(door, *face, c.u, c.v, v[kRollerHoleDiameter] / 2))
                return violation("Roller hole at (" + num(c.u) + ", " + num(c.v) + ") on face " +
                                     domain::toString(*face) + " of door " + quoted(door) +
                                     " would not lie on the door (" + num(door.length) + " x " + num(door.width) +
                                     " mm).",
                                 door.id.str());
            w.add(door, domain::Hole{*face, c.u, c.v, v[kRollerHoleDiameter], v[kRollerHoleDepth], false},
                  what + ": " + (row == 0 ? "bottom roller " : "top guide ") + (k == 0 ? "left" : "right"));
        }
    }
    Placement out;
    out.instances = {inst};
    out.operations = w.take();
    return out;
}

} // namespace fos::hardware
