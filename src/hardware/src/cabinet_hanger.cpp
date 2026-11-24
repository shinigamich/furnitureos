// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/hardware/cabinet_hanger.hpp"

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

std::string nameOf(const domain::HardwareDefinition& d) { return d.name.empty() ? d.id.str() : d.name; }

core::Error invalid(std::string message, const domain::HardwareDefinition& d) {
    return core::Error{core::ErrorCode::InvalidArgument, std::move(message), d.id.str()};
}
core::Error violation(std::string message, const std::string& entity) {
    return core::Error{core::ErrorCode::ConstraintViolation, std::move(message), entity};
}

/// Required hanger parameters, checked for consistency.
core::Result<std::map<std::string, double>> readHanger(const domain::HardwareDefinition& h) {
    if (h.kind != domain::HardwareKind::Suspension)
        return invalid("Hardware definition '" + h.id.str() + "' (" + h.name + ") is of kind " +
                           domain::toString(h.kind) + " but a cabinet hanger (Suspension) is required.",
                       h);
    std::map<std::string, double> v;
    for (const auto& key : requiredParams(h.kind)) {
        auto r = requireParam(h, key);
        if (!r) return r.error();
        v[key] = r.value();
    }
    using namespace keys;
    const std::string what = "cabinet hanger '" + nameOf(h) + "'";
    if (v[kBackSpaceMin] < v[kBodyDepth] - core::kLengthEps)
        return invalid("Parameter back_space_min (" + num(v[kBackSpaceMin]) + " mm) of " + what +
                           " is smaller than its body_depth (" + num(v[kBodyDepth]) +
                           " mm); the back panel would cut through the hanger body.",
                       h);
    const double r = v[kScrewDiameter] / 2;
    if (v[kScrewFromRearEdge] + r > v[kBodyDepth] + core::kLengthEps)
        return invalid("The screw holes of " + what + " (" + num(v[kScrewFromRearEdge]) + " mm from the rear edge, " +
                           num(v[kScrewDiameter]) + " mm diameter) lie outside its " + num(v[kBodyDepth]) +
                           " mm deep body.",
                       h);
    if (v[kScrewBelowTop] + v[kScrewSpacing] + r > v[kBodyHeight] + core::kLengthEps)
        return invalid("The lower screw hole of " + what + " (" + num(v[kScrewBelowTop] + v[kScrewSpacing]) +
                           " mm below the top) lies outside its " + num(v[kBodyHeight]) + " mm high body.",
                       h);
    return v;
}

} // namespace

core::Result<double> hangerBackSpace(const domain::HardwareDefinition& hanger) {
    auto v = readHanger(hanger);
    if (!v) return v.error();
    return v.value().at(keys::kBackSpaceMin);
}

core::Result<Placement> placeCabinetHanger(const Part& side, const Part& top, const domain::HardwareDefinition& hanger,
                                           const CabinetHangerConfig& cfg) {
    if (auto r = checkInstanceId(cfg.instanceId); !r) return r.error();
    for (const Part* p : {&side, &top})
        if (auto r = checkPart(*p); !r) return r.error();
    auto params = readHanger(hanger);
    if (!params) return params.error();
    auto& v = params.value();
    using namespace keys;
    const std::string what = nameOf(hanger);
    const std::string sid = side.id.str();

    // Inner face: the face of the side whose outward normal points towards the top's centre.
    const double d = dot(outwardNormal(side, Face::A), centreInAssembly(top) - centreInAssembly(side));
    if (std::fabs(d) <= core::kLengthEps)
        return violation("Cannot tell the inner face of " + quoted(side) + ": the centre of " + quoted(top) +
                             " lies in its mid-plane.",
                         sid);
    const Face face = d < 0 ? Face::B : Face::A;
    const Vec3 n = outwardNormal(side, face);
    if (!isParallel(n, {1, 0, 0}))
        return violation(quoted(side) + " must stand vertically with its faces towards the left and right (x) to "
                         "carry cabinet hanger '" + what + "'.",
                         sid);

    const machining::Range z = extentAlong(side, {0, 0, 1}), y = extentAlong(side, {0, 1, 0});
    const double ref = extentAlong(top, {0, 0, 1}).min;
    if (ref > z.max + core::kLengthEps || ref < z.min - core::kLengthEps)
        return violation("The underside of " + quoted(top) + " (z = " + num(ref) + ") is not within the height of " +
                             quoted(side) + " (" + num(z.min) + " .. " + num(z.max) + ").",
                         top.id.str());
    if (y.length() < v[kBodyDepth] - core::kLengthEps)
        return violation(quoted(side) + " is " + num(y.length()) + " mm deep, too shallow for the " +
                             num(v[kBodyDepth]) + " mm deep body of cabinet hanger '" + what + "'.",
                         sid);
    if (ref - z.min < v[kBodyHeight] - core::kLengthEps)
        return violation("Only " + num(ref - z.min) + " mm of " + quoted(side) + " remain below " + quoted(top) +
                             "; cabinet hanger '" + what + "' is " + num(v[kBodyHeight]) + " mm high.",
                         sid);
    const double depth = cfg.maxScrewDepth > 0 ? std::min(cfg.maxScrewDepth, v[kScrewDepth]) : v[kScrewDepth];
    const double m = cfg.fit.minRemainingMaterial;
    if (depth >= side.thickness - m - core::kLengthEps)
        return violation("The " + num(depth) + " mm deep screw holes of cabinet hanger '" + what + "' would leave " +
                             num(side.thickness - depth) + " mm of the " + num(side.thickness) + " mm thick " +
                             quoted(side) + "; more than " + num(m) + " mm must remain.",
                         sid);

    const double inner = dot(machining::faceFrameInAssembly(side, face).origin, n);
    OpWriter w(cfg.instanceId);
    domain::HardwareInstance inst;
    inst.id = cfg.instanceId;
    inst.definition = hanger.id;
    inst.quantity = 1;
    inst.parts = {side.id};
    inst.note = "Cabinet hanger on " + side.name + " (adjust height and depth after hanging)";
    for (int k = 0; k < 2; ++k) {
        Vec3 pt = n * inner;
        pt.y = y.max - v[kScrewFromRearEdge];
        pt.z = ref - v[kScrewBelowTop] - k * v[kScrewSpacing];
        if (k == 0) inst.position = pt;
        const auto c = machining::assemblyToFace(side, face, pt);
        if (!circleOnFace(side, face, c.u, c.v, v[kScrewDiameter] / 2))
            return violation("Hanger screw hole at (" + num(c.u) + ", " + num(c.v) + ") on face " +
                                 domain::toString(face) + " of " + quoted(side) + " would not lie on the panel (" +
                                 num(side.length) + " x " + num(side.width) + " mm).",
                             sid);
        w.add(side, domain::Hole{face, c.u, c.v, v[kScrewDiameter], depth, false},
              what + ": screw " + std::to_string(k + 1) + " of 2");
    }
    Placement out;
    out.instances = {inst};
    out.operations = w.take();
    return out;
}

core::Result<Placement> placeWallRail(const domain::HardwareDefinition& rail, const domain::HardwareDefinition& hanger,
                                      const WallRailConfig& cfg) {
    if (auto r = checkInstanceId(cfg.instanceId); !r) return r.error();
    auto hv = readHanger(hanger);
    if (!hv) return hv.error();
    using namespace keys;
    auto height = requireParam(rail, kRailHeight);
    if (!height) return height.error();
    auto thickness = requireParam(rail, kRailThickness);
    if (!thickness) return thickness.error();
    const std::string what = nameOf(rail);
    const double length = cfg.x1 - cfg.x0;
    if (!(length > core::kLengthEps))
        return violation("Wall rail '" + what + "' would be " + num(length) + " mm long; the length must be positive.",
                         rail.id.str());
    const double space = hv.value().at(kBackSpaceMin);
    if (thickness.value() >= space - core::kLengthEps)
        return violation("Wall rail '" + what + "' is " + num(thickness.value()) +
                             " mm thick but cabinet hanger '" + nameOf(hanger) + "' leaves only " + num(space) +
                             " mm behind the back panel; use a thinner rail.",
                         rail.id.str());
    const double zTop = cfg.referenceZ - hv.value().at(kRailTopBelowTop);
    domain::HardwareInstance inst;
    inst.id = cfg.instanceId;
    inst.definition = rail.id;
    inst.quantity = 1;
    inst.parts = cfg.parts;
    inst.position = {cfg.x0, cfg.wallY, zTop};
    inst.length = length;
    inst.note = "Wall rail " + num(length) + " mm (cut on site); top edge at z = " + num(zTop) + " mm, " +
                num(hv.value().at(kRailTopBelowTop)) + " mm below the underside of the panel above the hangers";
    Placement out;
    out.instances = {inst};
    return out;
}

} // namespace fos::hardware
