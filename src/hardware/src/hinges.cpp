// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/hardware/hinges.hpp"

#include "fos/hardware/catalog.hpp"
#include "util.hpp"

#include <cmath>

namespace fos::hardware {
using namespace detail;
using domain::Face;
using domain::Part;
using geometry::Vec3;

std::vector<HingeCountRule> defaultHingeCountRules() {
    return {{900, 2, 2, 3}, {1600, 3, 3, 4}, {2000, 4, 4, 5}, {2400, 5, 5, 6}};
}

core::Result<int> hingeCount(double doorHeight, DoorWeightClass weight, const std::vector<HingeCountRule>& rules) {
    const HingeCountRule* best = nullptr;
    double maxCovered = 0;
    for (const auto& r : rules) {
        maxCovered = std::max(maxCovered, r.maxDoorHeight);
        if (doorHeight <= r.maxDoorHeight + core::kLengthEps && (!best || r.maxDoorHeight < best->maxDoorHeight))
            best = &r;
    }
    if (!best)
        return core::makeError(core::ErrorCode::ConstraintViolation,
                               rules.empty() ? std::string("The hinge-count rule table is empty.")
                                             : "No hinge-count rule covers a door height of " + num(doorHeight) +
                                                   " mm (the table ends at " + num(maxCovered) +
                                                   " mm); give an explicit hinge count or extend the table.");
    const int n = weight == DoorWeightClass::Light ? best->light
                                                   : (weight == DoorWeightClass::Medium ? best->medium : best->heavy);
    if (n < 2)
        return core::makeError(core::ErrorCode::InvalidArgument,
                               "The hinge-count rule for doors up to " + num(best->maxDoorHeight) + " mm gives " +
                                   std::to_string(n) + " hinge(s); at least 2 are required.");
    return n;
}

core::Result<std::vector<double>> hingePositions(double doorHeight, int count, double offsetFromTop,
                                                 double offsetFromBottom) {
    if (count < 2)
        return core::makeError(core::ErrorCode::InvalidArgument,
                               "A door needs at least 2 hinges but " + std::to_string(count) + " were requested.");
    const double span = doorHeight - offsetFromTop - offsetFromBottom;
    if (offsetFromTop < 0 || offsetFromBottom < 0 || span <= core::kLengthEps)
        return core::makeError(core::ErrorCode::ConstraintViolation,
                               "Hinge offsets of " + num(offsetFromTop) + " mm (top) and " + num(offsetFromBottom) +
                                   " mm (bottom) leave no room on a door that is " + num(doorHeight) + " mm tall.");
    std::vector<double> out;
    for (int i = 0; i < count; ++i) out.push_back(offsetFromBottom + span * i / (count - 1));
    return out;
}

core::Result<Placement> placeHinges(const Part& door, const Part& side, const domain::HardwareDefinition& hinge,
                                    const HingeConfig& cfg) {
    if (auto r = checkInstanceId(cfg.instanceId); !r) return r.error();
    if (auto r = checkPart(door); !r) return r.error();
    if (auto r = checkPart(side); !r) return r.error();
    auto fail = [&](std::string m, const Part& p) {
        return core::makeError(core::ErrorCode::ConstraintViolation, std::move(m), p.id.str());
    };
    if (auto r = firstError(checkHingeFit(hinge, door, side, cfg.fit)); !r) return r.error();

    const Vec3 up{0, 0, 1};
    const Vec3 doorC = centreInAssembly(door), sideC = centreInAssembly(side);

    // Inner faces: the door face looking at the side, the side face looking at the door.
    Face doorFace = Face::A, sideFace = Face::A;
    const double dd = dot(outwardNormal(door, Face::A), sideC - doorC);
    const double ds = dot(outwardNormal(side, Face::A), doorC - sideC);
    if (std::fabs(dd) <= core::kLengthEps || std::fabs(ds) <= core::kLengthEps)
        return fail("Cannot tell which faces of door " + quoted(door) + " and " + quoted(side) +
                        " face each other: the panel centres lie in each other's mid-planes.", door);
    if (dd < 0) doorFace = Face::B;
    if (ds < 0) sideFace = Face::B;
    const Vec3 nD = outwardNormal(door, doorFace); // from the door into the cabinet
    const Vec3 nS = outwardNormal(side, sideFace); // from the side into the cabinet

    if (!isPerpendicular(nD, nS))
        return fail("Door " + quoted(door) + " is not perpendicular to " + quoted(side) +
                        "; hinges can only be placed between a door and a side at a right angle.", door);
    if (!isPerpendicular(nD, up) || !isPerpendicular(nS, up))
        return fail("Door " + quoted(door) + " and " + quoted(side) +
                        " must both be vertical (hinge axis along the assembly Z axis); flap doors are not supported.",
                    door);
    const auto df = machining::faceFrameInAssembly(door, doorFace);
    if (!isParallel(df.uAxis, up) && !isParallel(df.vAxis, up))
        return fail("Door " + quoted(door) + " is rotated in its own plane; its length or width must be vertical.", door);
    const auto sf = machining::faceFrameInAssembly(side, sideFace);
    if (!isParallel(sf.uAxis, up) && !isParallel(sf.vAxis, up))
        return fail(quoted(side) + " is rotated in its own plane; its length or width must be vertical.", side);

    const machining::Range doorZ = extentAlong(door, up);      // door bottom/top
    const machining::Range doorS = extentAlong(door, nS);      // door across its width, towards the cabinet inside
    const double doorHeight = doorZ.length(), doorWidth = doorS.length();
    const double sideInner = dot(sf.origin, nS);
    const double hingedEdge = doorS.min; // the vertical door edge at the side
    if (hingedEdge > sideInner + core::kLengthEps)
        return fail("The hinged edge of door " + quoted(door) + " lies " + num(hingedEdge - sideInner) +
                        " mm inside the inner face of " + quoted(side) +
                        "; inset doors and doors not reaching the side are not supported by this rule.", door);
    if (hingedEdge < sideInner - side.thickness - cfg.maxSideOverhang - core::kLengthEps)
        return fail("The hinged edge of door " + quoted(door) + " projects " +
                        num(sideInner - side.thickness - hingedEdge) + " mm beyond the outer face of " + quoted(side) +
                        " (allowed " + num(cfg.maxSideOverhang) + " mm); the door does not hang on this side.", door);
    if (doorS.max <= sideInner + core::kLengthEps)
        return fail("Door " + quoted(door) + " does not cover the cabinet opening next to " + quoted(side) + ".", door);

    const double sideFront = extentAlong(side, nD).min; // front edge of the side
    const double doorInner = dot(df.origin, nD);
    const double frontGap = sideFront - doorInner;
    if (frontGap < -0.1 || frontGap > cfg.maxFrontGap + core::kLengthEps)
        return fail("The inner face of door " + quoted(door) + " is " + num(frontGap) + " mm in front of the front edge of " +
                        quoted(side) + "; it must be within 0.." + num(cfg.maxFrontGap) +
                        " mm for an overlay door on this side.", door);

    const double cupD = hinge.params.at(keys::kCupDiameter), cupDepth = hinge.params.at(keys::kCupDepth);
    const double cupC = hinge.params.at(keys::kCupCenterFromDoorEdge);
    const double plateLine = hinge.params.at(keys::kPlateLineFromFrontEdge);
    const double plateSpacing = hinge.params.at(keys::kPlateHoleSpacing);
    const double plateD = hinge.params.at(keys::kPlateHoleDiameter), plateDepth = hinge.params.at(keys::kPlateHoleDepth);
    if (cupC - cupD / 2 < -core::kLengthEps || cupC + cupD / 2 > doorWidth + core::kLengthEps)
        return fail("Door " + quoted(door) + " is " + num(doorWidth) + " mm wide, too narrow for a " + num(cupD) +
                        " mm hinge cup centred " + num(cupC) + " mm from its hinged edge (needs at least " +
                        num(cupC + cupD / 2) + " mm).", door);

    core::Result<std::vector<double>> heights = std::vector<double>{};
    int count = cfg.count;
    if (!cfg.positions.empty()) {
        const auto& ps = cfg.positions;
        if (ps.size() < 2)
            return fail("Door " + quoted(door) + " needs at least 2 hinges but " + std::to_string(ps.size()) +
                            " explicit hinge position was given.",
                        door);
        for (std::size_t i = 0; i < ps.size(); ++i) {
            if (!std::isfinite(ps[i]) || ps[i] <= core::kLengthEps || ps[i] >= doorHeight - core::kLengthEps)
                return fail("Door " + quoted(door) + ": hinge position " + num(ps[i]) +
                                " mm is outside the door height (0 .. " + num(doorHeight) + " mm).",
                            door);
            if (i > 0 && ps[i] <= ps[i - 1] + core::kLengthEps)
                return fail("Door " + quoted(door) + ": explicit hinge positions must be strictly ascending (" +
                                num(ps[i - 1]) + " mm is followed by " + num(ps[i]) + " mm).",
                            door);
        }
        heights = ps;
        count = static_cast<int>(ps.size());
    } else {
        if (count == 0) {
            auto n = hingeCount(doorHeight, cfg.weightClass, cfg.rules);
            if (!n)
                return core::makeError(n.error().code, "Door " + quoted(door) + ": " + n.error().message, door.id.str());
            count = n.value();
        }
        heights = hingePositions(doorHeight, count, cfg.offsetFromTop, cfg.offsetFromBottom);
        if (!heights)
            return core::makeError(heights.error().code, "Door " + quoted(door) + ": " + heights.error().message,
                                   door.id.str());
    }

    const std::string what = hinge.name.empty() ? hinge.id.str() : hinge.name;
    OpWriter w(cfg.instanceId);
    domain::HardwareInstance inst;
    inst.id = cfg.instanceId;
    inst.definition = hinge.id;
    inst.quantity = count;
    inst.parts = {door.id, side.id};
    inst.note = "Hangs " + door.name + " on " + side.name;

    // (nS, up, nD) is an orthonormal basis of the assembly frame, so a point is given by
    // its three coordinates along these directions.
    auto point = [&](double s, double z, double d) { return nS * s + up * z + nD * d; };
    int i = 0;
    for (const double h : heights.value()) {
        ++i;
        const double z = doorZ.min + h;
        const Vec3 cup = point(hingedEdge + cupC, z, doorInner);
        const auto dc = machining::assemblyToFace(door, doorFace, cup);
        if (i == 1) inst.position = cup;
        w.add(door, domain::Hole{doorFace, dc.u, dc.v, cupD, cupDepth, false},
              what + ": cup, hinge " + std::to_string(i) + " of " + std::to_string(count));
        for (const double dz : {-plateSpacing / 2, plateSpacing / 2}) {
            const auto sc = machining::assemblyToFace(side, sideFace, point(sideInner, z + dz, sideFront + plateLine));
            if (!circleOnFace(side, sideFace, sc.u, sc.v, plateD / 2))
                return fail("The mounting-plate hole of hinge " + std::to_string(i) + " for door " + quoted(door) +
                                " at (" + num(sc.u) + ", " + num(sc.v) + ") on face " + domain::toString(sideFace) +
                                " of " + quoted(side) + " would not lie completely on that panel (" +
                                num(side.length) + " x " + num(side.width) + " mm).", side);
            w.add(side, domain::Hole{sideFace, sc.u, sc.v, plateD, plateDepth, false},
                  what + ": mounting plate, hinge " + std::to_string(i) + " of " + std::to_string(count));
        }
    }
    Placement out;
    out.instances = {inst};
    out.operations = w.take();
    return out;
}

} // namespace fos::hardware
