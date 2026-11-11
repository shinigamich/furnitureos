// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/hardware/corner_joint.hpp"

#include "fos/hardware/catalog.hpp"
#include "util.hpp"

namespace fos::hardware {
using namespace detail;
using domain::Face;
using domain::Part;
using geometry::Vec3;

core::Result<ButtJoint> detectButtJoint(const Part& facePart, const Part& edgePart, double tolerance) {
    if (auto r = checkPart(facePart); !r) return r.error();
    if (auto r = checkPart(edgePart); !r) return r.error();
    const std::string pair = "edge of " + quoted(edgePart) + " and face of " + quoted(facePart);
    std::string partial; // best explanation found for a near miss

    for (Face e : {Face::L1, Face::L2, Face::W1, Face::W2}) {
        const auto fe = machining::faceFrameInAssembly(edgePart, e);
        const auto ee = machining::faceExtents(edgePart, e);
        for (Face f : {Face::A, Face::B}) {
            const auto ff = machining::faceFrameInAssembly(facePart, f);
            if (dot(fe.toolDir, ff.toolDir) > -1 + kAngleEps) continue; // surfaces must face each other
            // Signed distance of the edge surface in front of the face surface.
            const double gap = dot(fe.origin - ff.origin, -ff.toolDir);
            if (std::fabs(gap) > tolerance) {
                if (partial.empty() && gap > 0 && gap < 50)
                    partial = "Edge " + std::string(domain::toString(e)) + " of " + quoted(edgePart) + " is " +
                              num(gap) + " mm away from face " + domain::toString(f) + " of " + quoted(facePart) +
                              " (allowed " + num(tolerance) + " mm).";
                continue;
            }
            if (!(isParallel(fe.uAxis, ff.uAxis) || isPerpendicular(fe.uAxis, ff.uAxis))) {
                partial = "Edge " + std::string(domain::toString(e)) + " of " + quoted(edgePart) +
                          " lies on face " + domain::toString(f) + " of " + quoted(facePart) +
                          " but the panels are rotated against each other; only axis-aligned joints are supported.";
                continue;
            }
            // Rectangle of the face part's face, expressed in the edge face's (u, v).
            const auto fext = machining::faceExtents(facePart, f);
            machining::Range ru{1e300, -1e300}, rv{1e300, -1e300};
            for (int i = 0; i < 4; ++i) {
                const Vec3 p = ff.origin + ff.uAxis * ((i & 1) ? fext.u.max : 0.0) + ff.vAxis * ((i & 2) ? fext.v.max : 0.0);
                const double u = dot(p - fe.origin, fe.uAxis), v = dot(p - fe.origin, fe.vAxis);
                ru = {std::min(ru.min, u), std::max(ru.max, u)};
                rv = {std::min(rv.min, v), std::max(rv.max, v)};
            }
            const machining::Range along{std::max(ru.min, 0.0), std::min(ru.max, ee.u.max)};
            const machining::Range across{std::max(rv.min, 0.0), std::min(rv.max, ee.v.max)};
            if (along.length() <= tolerance || across.length() <= tolerance) {
                if (partial.empty())
                    partial = "Edge " + std::string(domain::toString(e)) + " of " + quoted(edgePart) +
                              " is in the plane of face " + domain::toString(f) + " of " + quoted(facePart) +
                              " but the two surfaces do not overlap.";
                continue;
            }
            if (across.min > tolerance || across.max < ee.v.max - tolerance) {
                partial = "Edge " + std::string(domain::toString(e)) + " of " + quoted(edgePart) +
                          " only partly rests on face " + domain::toString(f) + " of " + quoted(facePart) + ": " +
                          num(across.length()) + " mm of its " + num(ee.v.max) + " mm thickness are supported.";
                continue;
            }
            return ButtJoint{f, e, along};
        }
    }
    return core::makeError(core::ErrorCode::ConstraintViolation,
                           "No butt joint between " + pair + ": " +
                               (partial.empty() ? "no edge face of " + quoted(edgePart) + " lies against face A or B of " +
                                                      quoted(facePart) + " (tolerance " + num(tolerance) + " mm)."
                                                : partial),
                           edgePart.id.str());
}

core::Result<std::vector<double>> fastenerPositions(double jointLength, double minEndOffset, double maxSpacing,
                                                    int count) {
    const double eps = core::kLengthEps;
    auto fail = [](std::string m) { return core::makeError(core::ErrorCode::ConstraintViolation, std::move(m)); };
    if (!(jointLength > 0)) return fail("The joint length must be positive but is " + num(jointLength) + " mm.");
    if (minEndOffset < 0 || count < 0)
        return fail("The fastener end offset (" + num(minEndOffset) + " mm) and count (" + std::to_string(count) +
                    ") must not be negative.");
    if (count == 1) return std::vector<double>{jointLength / 2};
    const double span = jointLength - 2 * minEndOffset;
    int n = count;
    if (n == 0) {
        if (span < -eps)
            return fail("The joint is " + num(jointLength) + " mm long, too short for fasteners " +
                        num(minEndOffset) + " mm from both ends (needs at least " + num(2 * minEndOffset) +
                        " mm); reduce the end offset or give an explicit count of 1.");
        if (span <= eps) return std::vector<double>{jointLength / 2};
        if (!(maxSpacing > 0)) return fail("The maximum fastener spacing must be positive but is " + num(maxSpacing) + " mm.");
        n = std::max(2, static_cast<int>(std::ceil(span / maxSpacing - 1e-9)) + 1);
    } else if (span <= eps) {
        return fail("Cannot place " + std::to_string(count) + " fasteners " + num(minEndOffset) +
                    " mm from both ends of a joint that is only " + num(jointLength) + " mm long.");
    }
    std::vector<double> out;
    for (int i = 0; i < n; ++i) out.push_back(minEndOffset + span * i / (n - 1));
    return out;
}

core::Result<Placement> connectCorner(const Part& facePart, const Part& edgePart,
                                      const domain::HardwareDefinition& fastenerIn, const CornerJointConfig& cfg) {
    if (auto r = checkInstanceId(cfg.instanceId); !r) return r.error();
    const bool confirmat = fastenerIn.kind == domain::HardwareKind::Confirmat;
    const bool dowel = fastenerIn.kind == domain::HardwareKind::Dowel;
    if (!confirmat && !dowel)
        return core::makeError(core::ErrorCode::Unsupported,
                               "connectCorner supports fasteners of kind Confirmat or Dowel, but '" +
                                   fastenerIn.id.str() + "' is of kind " + domain::toString(fastenerIn.kind) + ".",
                               fastenerIn.id.str());
    domain::HardwareDefinition fastener = fastenerIn;
    if (dowel && cfg.dowelFaceDepth) fastener.params[keys::kFaceDepth] = *cfg.dowelFaceDepth;
    if (dowel && cfg.dowelEdgeDepth) fastener.params[keys::kEdgeDepth] = *cfg.dowelEdgeDepth;

    auto joint = detectButtJoint(facePart, edgePart, cfg.contactTolerance);
    if (!joint) return joint.error();
    const ButtJoint j = joint.value();

    const auto fit = confirmat ? checkConfirmatFit(fastener, facePart, edgePart, j.edgeOnEdgePart, cfg.fit)
                               : checkDowelFit(fastener, facePart, edgePart, j.edgeOnEdgePart, cfg.fit);
    if (auto r = firstError(fit); !r) return r.error();

    auto positions = fastenerPositions(j.along.length(), cfg.minEndOffset, cfg.maxSpacing, cfg.count);
    if (!positions)
        return core::makeError(core::ErrorCode::ConstraintViolation,
                               "Joint between " + quoted(edgePart) + " and " + quoted(facePart) + ": " +
                                   positions.error().message,
                               edgePart.id.str());

    const double faceD = fastener.params.at(confirmat ? keys::kThroughDiameter : keys::kDiameter);
    const double edgeD = fastener.params.at(confirmat ? keys::kPilotDiameter : keys::kDiameter);
    const double edgeDepth = confirmat ? fastener.params.at(keys::kLength) - facePart.thickness +
                                             fastener.params.at(keys::kPilotExtraDepth)
                                       : fastener.params.at(keys::kEdgeDepth);
    const double vCentre = edgePart.thickness / 2;
    const std::string what = fastener.name.empty() ? fastener.id.str() : fastener.name;

    OpWriter w(cfg.instanceId);
    domain::HardwareInstance inst;
    inst.id = cfg.instanceId;
    inst.definition = fastener.id;
    inst.quantity = static_cast<int>(positions.value().size());
    inst.parts = {facePart.id, edgePart.id};
    inst.note = "Joins edge " + std::string(domain::toString(j.edgeOnEdgePart)) + " of " + quoted(edgePart) +
                " to face " + domain::toString(j.faceOnFacePart) + " of " + quoted(facePart);

    bool first = true;
    for (const double s : positions.value()) {
        const double u = j.along.min + s;
        const Vec3 p = machining::faceToAssembly(edgePart, j.edgeOnEdgePart, u, vCentre);
        const auto fc = machining::assemblyToFace(facePart, j.faceOnFacePart, p);
        if (!circleOnFace(facePart, j.faceOnFacePart, fc.u, fc.v, faceD / 2))
            return core::makeError(core::ErrorCode::ConstraintViolation,
                                   "The " + num(faceD) + " mm hole for " + what + " at (" + num(fc.u) + ", " +
                                       num(fc.v) + ") on face " + domain::toString(j.faceOnFacePart) + " of " +
                                       quoted(facePart) + " would not lie completely on the panel (" +
                                       num(facePart.length) + " x " + num(facePart.width) + " mm).",
                                   facePart.id.str());
        if (first) inst.position = p;
        first = false;
        if (confirmat)
            w.add(facePart, domain::Hole{machining::oppositeFace(j.faceOnFacePart), fc.u, fc.v, faceD, 0.0, true},
                  what + ": through hole");
        else
            w.add(facePart, domain::Hole{j.faceOnFacePart, fc.u, fc.v, faceD, fastener.params.at(keys::kFaceDepth), false},
                  what + ": face hole");
        w.add(edgePart, domain::Hole{j.edgeOnEdgePart, u, vCentre, edgeD, edgeDepth, false},
              what + (confirmat ? ": pilot hole" : ": edge hole"));
    }
    Placement out;
    out.instances = {inst};
    out.operations = w.take();
    return out;
}

} // namespace fos::hardware
