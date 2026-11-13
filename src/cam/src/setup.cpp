// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/cam/setup.hpp"

#include "fos/core/units.hpp"
#include "fos/machining/face.hpp"
#include "fos/machining/flip.hpp"

#include <cmath>

namespace fos::cam {

using core::ErrorCode;
using core::makeError;
using domain::Face;

const char* toString(Disposition d) noexcept {
    switch (d) {
    case Disposition::MachinableNow: return "machinable now";
    case Disposition::NeedsOtherSetup: return "needs other setup";
    case Disposition::Unsupported: return "unsupported";
    }
    return "unknown";
}

geometry::Vec3 SetupFrame::partToMachine(geometry::Vec3 p) const noexcept {
    if (faceUp == Face::B) return origin + geometry::Vec3{p.x, width - p.y, thickness - p.z};
    return origin + p;
}

core::Result<SetupFrame> makeSetupFrame(const domain::Part& part, const Setup& setup) {
    if (setup.faceUp != Face::A && setup.faceUp != Face::B)
        return makeError(ErrorCode::Unsupported,
                         std::string("Face ") + domain::toString(setup.faceUp) + " of '" + part.name +
                             "' cannot face the spindle: a 3-axis vertical-spindle setup supports face A up or face B up only.",
                         part.id.str());
    if (!(part.length > 0 && part.width > 0 && part.thickness > 0))
        return makeError(ErrorCode::InvalidArgument,
                         "Part '" + part.name + "' has non-positive dimensions " + core::formatLength(part.length, 3) +
                             " x " + core::formatLength(part.width, 3) + " x " + core::formatLength(part.thickness, 3) +
                             " mm.",
                         part.id.str());
    if (!(setup.stockOrigin.z >= -core::kLengthEps))
        return makeError(ErrorCode::InvalidArgument,
                         "Setup of '" + part.name + "' puts the stock bottom at machine Z " +
                             core::formatLength(setup.stockOrigin.z, 3) +
                             " mm, below Z = 0. The machine frame requires Z = 0 at the spoilboard top and the stock "
                             "resting on or above it (the spoilboard limit is measured from the stock bottom); set the "
                             "work zero on the spoilboard.",
                         part.id.str());
    SetupFrame f;
    f.faceUp = setup.faceUp;
    f.origin = setup.stockOrigin;
    f.length = part.length;
    f.width = part.width;
    f.thickness = part.thickness;
    f.bottomZ = f.origin.z;
    f.topZ = f.origin.z + part.thickness;
    f.stock = {f.origin, f.origin + geometry::Vec3{part.length, part.width, part.thickness}};
    return f;
}

geometry::Vec2 faceToMachineXY(const domain::Part& part, const SetupFrame& frame, geometry::Vec2 uv) noexcept {
    geometry::Vec2 view = uv;
    if (frame.faceUp == Face::B) view = machining::flippedFaceCoordinates(part, Face::B, uv, machining::FlipAxis::Length);
    return {frame.origin.x + view.x, frame.origin.y + view.y};
}

core::Result<SetupPlan> planSetup(const domain::Part& part, const Setup& setup) {
    auto frame = makeSetupFrame(part, setup);
    if (!frame) return frame.error();
    SetupPlan plan;
    for (std::size_t i = 0; i < part.operations.size(); ++i) {
        const auto& op = part.operations[i];
        PlannedOperation p;
        p.index = i;
        p.id = op.id;
        const Face face = domain::faceOf(op);
        const std::string name = "Operation '" + op.id.str() + "' on face " + domain::toString(face) + " of '" +
                                 part.name + "'";
        if (std::holds_alternative<domain::Pocket>(op.shape)) {
            p.disposition = Disposition::Unsupported;
            p.reason = name + " is a pocket; pocket milling is not implemented in the CAM module (no pocket "
                              "strategy verified by the simulator exists). Machine it by other means.";
            plan.unsupported.push_back(std::move(p));
        } else if (machining::isEdgeFace(face)) {
            p.disposition = Disposition::Unsupported;
            p.reason = name + " is on an edge face; it needs a horizontal spindle or an angle aggregate, which a "
                              "3-axis vertical-spindle machine profile does not have.";
            plan.unsupported.push_back(std::move(p));
        } else if (face != setup.faceUp) {
            p.disposition = Disposition::NeedsOtherSetup;
            p.requiredFaceUp = face;
            p.reason = name + " needs a setup with face " + domain::toString(face) + " up (current setup: face " +
                       domain::toString(setup.faceUp) + " up).";
            plan.needsOtherSetup.push_back(std::move(p));
        } else {
            p.disposition = Disposition::MachinableNow;
            plan.machinableNow.push_back(std::move(p));
        }
    }
    return plan;
}

HoleTarget holeTarget(const domain::Part& part, const SetupFrame& frame, const domain::Operation& op) {
    const auto& h = std::get<domain::Hole>(op.shape);
    HoleTarget t;
    t.id = op.id;
    t.centre = faceToMachineXY(part, frame, {h.u, h.v});
    t.radius = h.diameter / 2;
    t.topZ = frame.topZ;
    t.through = h.through;
    t.floorZ = h.through ? frame.bottomZ : frame.topZ - h.depth;
    return t;
}

namespace {
/// True if `p` lies on the face boundary and `outward` (unit, axis-parallel) leaves the face there.
bool endIsOpen(geometry::Vec2 p, geometry::Vec2 outward, double uMax, double vMax) {
    constexpr double eps = 1e-6;
    const bool alongU = std::fabs(outward.y) < eps, alongV = std::fabs(outward.x) < eps;
    if (alongU) return (outward.x > 0 && std::fabs(p.x - uMax) < eps) || (outward.x < 0 && std::fabs(p.x) < eps);
    if (alongV) return (outward.y > 0 && std::fabs(p.y - vMax) < eps) || (outward.y < 0 && std::fabs(p.y) < eps);
    return false;
}
} // namespace

GrooveTarget grooveTarget(const domain::Part& part, const SetupFrame& frame, const domain::Operation& op) {
    const auto& g = std::get<domain::Groove>(op.shape);
    GrooveTarget t;
    t.id = op.id;
    t.start = faceToMachineXY(part, frame, {g.u1, g.v1});
    t.end = faceToMachineXY(part, frame, {g.u2, g.v2});
    t.halfWidth = g.width / 2;
    t.topZ = frame.topZ;
    t.floorZ = frame.topZ - g.depth;
    const geometry::Vec2 a{g.u1, g.v1}, b{g.u2, g.v2};
    const double len = geometry::length(b - a);
    if (len > core::kLengthEps) {
        const geometry::Vec2 d = (b - a) * (1.0 / len);
        t.startOpen = endIsOpen(a, -d, part.length, part.width);
        t.endOpen = endIsOpen(b, d, part.length, part.width);
    }
    return t;
}

} // namespace fos::cam
