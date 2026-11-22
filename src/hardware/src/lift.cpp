// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/hardware/lift.hpp"

#include "fos/hardware/catalog.hpp"
#include "util.hpp"

#include <cmath>

namespace fos::hardware {
using namespace detail;
using domain::Face;
using domain::Part;
using geometry::Vec3;

double liftPowerFactor(double flapHeight, double flapWeightKg) noexcept { return flapHeight * flapWeightKg; }

core::Result<Placement> placeLiftMechanism(const Part& flap, const Part& side, const Part& top,
                                           const domain::HardwareDefinition& lift, const LiftConfig& cfg) {
    if (auto r = checkInstanceId(cfg.instanceId); !r) return r.error();
    for (const Part* p : {&flap, &side, &top})
        if (auto r = checkPart(*p); !r) return r.error();
    auto fail = [](std::string m, const Part& p) {
        return core::makeError(core::ErrorCode::ConstraintViolation, std::move(m), p.id.str());
    };
    const std::string what = lift.name.empty() ? lift.id.str() : lift.name;
    if (lift.kind != domain::HardwareKind::LiftMechanism)
        return core::makeError(core::ErrorCode::InvalidArgument,
                               "Hardware definition '" + lift.id.str() + "' (" + lift.name + ") is of kind " +
                                   domain::toString(lift.kind) + " but a lift mechanism is required.",
                               lift.id.str());
    std::map<std::string, double> v;
    for (const auto& key : requiredParams(lift.kind)) {
        auto r = requireParam(lift, key);
        if (!r) return r.error();
        v[key] = r.value();
    }
    using namespace keys;

    const Vec3 up{0, 0, 1};
    const Vec3 flapC = centreInAssembly(flap), sideC = centreInAssembly(side);
    const double df = dot(outwardNormal(flap, Face::A), sideC - flapC);
    const double ds = dot(outwardNormal(side, Face::A), flapC - sideC);
    if (std::fabs(df) <= core::kLengthEps || std::fabs(ds) <= core::kLengthEps)
        return fail("Cannot tell which faces of flap " + quoted(flap) + " and " + quoted(side) +
                        " face each other: the panel centres lie in each other's mid-planes.", flap);
    const Face flapFace = df < 0 ? Face::B : Face::A, sideFace = ds < 0 ? Face::B : Face::A;
    const Vec3 nD = outwardNormal(flap, flapFace); // from the flap into the cabinet
    const Vec3 nS = outwardNormal(side, sideFace); // from the side into the cabinet
    if (!isPerpendicular(nD, up) || !isPerpendicular(nS, up) || !isPerpendicular(nD, nS))
        return fail("Flap " + quoted(flap) + " and " + quoted(side) +
                        " must both be vertical and at a right angle to each other for a lift mechanism.", flap);

    const machining::Range sideZ = extentAlong(side, up), sideD = extentAlong(side, nD);
    const double height = sideZ.length(), depth = sideD.length();
    if (height < v[kCabinetHeightMin] - core::kLengthEps)
        return fail("The cabinet is too low for lift mechanism '" + what + "': " + quoted(side) + " is " + num(height) +
                        " mm tall, the mechanism needs a cabinet height of " + num(v[kCabinetHeightMin]) + " .. " +
                        num(v[kCabinetHeightMax]) + " mm.", side);
    if (height > v[kCabinetHeightMax] + core::kLengthEps)
        return fail("The cabinet is too tall for lift mechanism '" + what + "': " + quoted(side) + " is " +
                        num(height) + " mm tall, the mechanism covers cabinet heights of " +
                        num(v[kCabinetHeightMin]) + " .. " + num(v[kCabinetHeightMax]) + " mm.", side);
    if (depth < v[kCabinetDepthMin] - core::kLengthEps)
        return fail("The cabinet is too shallow for lift mechanism '" + what + "': " + quoted(side) + " is " +
                        num(depth) + " mm deep, the mechanism body needs at least " + num(v[kCabinetDepthMin]) + " mm.",
                    side);
    const machining::Range flapZ = extentAlong(flap, up);
    if (cfg.flapWeightKg > 0) {
        const double pf = liftPowerFactor(flapZ.length(), cfg.flapWeightKg);
        if (pf < v[kPowerFactorMin] - core::kLengthEps || pf > v[kPowerFactorMax] + core::kLengthEps)
            return fail("Flap " + quoted(flap) + " (" + num(flapZ.length()) + " mm high, " + num(cfg.flapWeightKg) +
                            " kg) has a power factor of " + num(pf) + "; lift mechanism '" + what + "' is rated for " +
                            num(v[kPowerFactorMin]) + " .. " + num(v[kPowerFactorMax]) + ".", flap);
    }

    const double sideDepth = cfg.maxSideHoleDepth > 0 ? std::min(cfg.maxSideHoleDepth, v[kSideHoleDepth])
                                                      : v[kSideHoleDepth];
    const double m = cfg.fit.minRemainingMaterial;
    if (sideDepth >= side.thickness - m - core::kLengthEps)
        return fail("The " + num(sideDepth) + " mm deep holes of lift mechanism '" + what + "' would leave " +
                        num(side.thickness - sideDepth) + " mm of the " + num(side.thickness) + " mm thick " +
                        quoted(side) + "; more than " + num(m) + " mm must remain.", side);
    if (v[kFlapHoleDepth] >= flap.thickness - m - core::kLengthEps)
        return fail("The " + num(v[kFlapHoleDepth]) + " mm deep holes of lift mechanism '" + what + "' would leave " +
                        num(flap.thickness - v[kFlapHoleDepth]) + " mm of the " + num(flap.thickness) +
                        " mm thick flap " + quoted(flap) + "; more than " + num(m) + " mm must remain.", flap);

    const double topUnder = extentAlong(top, up).min;
    if (topUnder > sideZ.max + core::kLengthEps || topUnder < sideZ.min - core::kLengthEps)
        return fail("The underside of " + quoted(top) + " (z = " + num(topUnder) + ") is not within the height of " +
                        quoted(side) + " (" + num(sideZ.min) + " .. " + num(sideZ.max) + ").", top);
    const double sideInner = dot(machining::faceFrameInAssembly(side, sideFace).origin, nS);
    const double flapInner = dot(machining::faceFrameInAssembly(flap, flapFace).origin, nD);
    const double flapEdge = extentAlong(flap, nS).min; // flap edge nearest the side
    auto point = [&](double s, double z, double d) { return nS * s + up * z + nD * d; };

    OpWriter w(cfg.instanceId);
    domain::HardwareInstance inst;
    inst.id = cfg.instanceId;
    inst.definition = lift.id;
    inst.quantity = 1;
    inst.parts = {flap.id, side.id};
    inst.note = "Lifts " + flap.name + " on " + side.name;
    for (int k = 0; k < 2; ++k) {
        const Vec3 pt = point(flapEdge + v[kFlapHoleFromSideEdge],
                              flapZ.max - v[kFlapHoleFromTopEdge] - k * v[kFlapHoleSpacing], flapInner);
        const auto c = machining::assemblyToFace(flap, flapFace, pt);
        if (!circleOnFace(flap, flapFace, c.u, c.v, v[kFlapHoleDiameter] / 2))
            return fail("Lift mechanism hole at (" + num(c.u) + ", " + num(c.v) + ") on face " +
                            domain::toString(flapFace) + " of flap " + quoted(flap) + " would not lie on the panel (" +
                            num(flap.length) + " x " + num(flap.width) + " mm).", flap);
        w.add(flap, domain::Hole{flapFace, c.u, c.v, v[kFlapHoleDiameter], v[kFlapHoleDepth], false},
              what + ": flap fixing " + std::to_string(k + 1) + " of 2");
    }
    for (int k = 0; k < 2; ++k) {
        const Vec3 pt = point(sideInner, topUnder - v[kSideHoleBelowTop],
                              sideD.min + v[kSideHoleFromFront] + k * v[kSideHoleSpacing]);
        if (k == 0) inst.position = pt;
        const auto c = machining::assemblyToFace(side, sideFace, pt);
        if (!circleOnFace(side, sideFace, c.u, c.v, v[kSideHoleDiameter] / 2))
            return fail("Lift mechanism hole at (" + num(c.u) + ", " + num(c.v) + ") on face " +
                            domain::toString(sideFace) + " of " + quoted(side) + " would not lie on the panel (" +
                            num(side.length) + " x " + num(side.width) + " mm).", side);
        w.add(side, domain::Hole{sideFace, c.u, c.v, v[kSideHoleDiameter], sideDepth, false},
              what + ": body fixing " + std::to_string(k + 1) + " of 2");
    }
    Placement out;
    out.instances = {inst};
    out.operations = w.take();
    return out;
}

} // namespace fos::hardware
