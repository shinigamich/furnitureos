// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/hardware/handle.hpp"

#include "fos/hardware/catalog.hpp"
#include "util.hpp"

namespace fos::hardware {
using namespace detail;
using domain::Part;
using geometry::Vec3;

core::Result<Placement> placeHandle(const Part& front, const domain::HardwareDefinition& handle,
                                    const HandleConfig& cfg) {
    if (auto r = checkInstanceId(cfg.instanceId); !r) return r.error();
    if (auto r = checkPart(front); !r) return r.error();
    auto fail = [&](std::string m) {
        return core::makeError(core::ErrorCode::ConstraintViolation, std::move(m), front.id.str());
    };
    if (handle.kind != domain::HardwareKind::Handle)
        return core::makeError(core::ErrorCode::Unsupported,
                               "placeHandle needs a definition of kind Handle, but '" + handle.id.str() +
                                   "' is of kind " + domain::toString(handle.kind) + ".",
                               handle.id.str());
    const auto spacingR = requireParam(handle, keys::kHoleSpacing);
    if (!spacingR) return spacingR.error();
    const auto diaR = requireParam(handle, keys::kHoleDiameter);
    if (!diaR) return diaR.error();
    const double spacing = spacingR.value(), d = diaR.value();
    if (machining::isEdgeFace(cfg.face))
        return fail("Handle holes must be placed on face A or B of " + quoted(front) + ", not on edge face " +
                    domain::toString(cfg.face) + ".");

    const Vec3 up{0, 0, 1};
    const auto ff = machining::faceFrameInAssembly(front, cfg.face);
    const Vec3 n = -ff.toolDir;
    if (!isPerpendicular(n, up))
        return fail("Front " + quoted(front) + " is not vertical; handle positions are defined for vertical fronts only.");
    Vec3 h;
    if (isParallel(ff.uAxis, up)) h = ff.vAxis;
    else if (isParallel(ff.vAxis, up)) h = ff.uAxis;
    else return fail("Front " + quoted(front) + " is rotated in its own plane; its length or width must be vertical.");
    // Orient "horizontal" so that it increases to the right (larger assembly X, else Y).
    if (std::fabs(h.x) > kAngleEps ? h.x < 0 : h.y < 0) h = -h;

    const machining::Range rh = extentAlong(front, h), rz = extentAlong(front, up);
    double hc = (rh.min + rh.max) / 2, zc = (rz.min + rz.max) / 2;
    if (cfg.horizontalAnchor == HorizontalAnchor::FromLeft) hc = rh.min + cfg.horizontalOffset;
    if (cfg.horizontalAnchor == HorizontalAnchor::FromRight) hc = rh.max - cfg.horizontalOffset;
    if (cfg.verticalAnchor == VerticalAnchor::FromBottom) zc = rz.min + cfg.verticalOffset;
    if (cfg.verticalAnchor == VerticalAnchor::FromTop) zc = rz.max - cfg.verticalOffset;

    const double plane = dot(ff.origin, n);
    auto point = [&](double hh, double z) { return h * hh + up * z + n * plane; }; // (h, up, n) orthonormal
    const bool horizontal = cfg.orientation == HandleOrientation::Horizontal;
    const std::string what = handle.name.empty() ? handle.id.str() : handle.name;

    OpWriter w(cfg.instanceId);
    for (const double s : {-spacing / 2, spacing / 2}) {
        const double hh = horizontal ? hc + s : hc, z = horizontal ? zc : zc + s;
        const auto fc = machining::assemblyToFace(front, cfg.face, point(hh, z));
        if (!circleOnFace(front, cfg.face, fc.u, fc.v, d / 2, cfg.minEdgeMaterial)) {
            const double margin = std::min(std::min(hh - rh.min, rh.max - hh), std::min(z - rz.min, rz.max - z)) - d / 2;
            return fail("A " + num(d) + " mm handle hole at (" + num(fc.u) + ", " + num(fc.v) + ") on face " +
                        domain::toString(cfg.face) + " of " + quoted(front) + " (" + num(front.length) + " x " +
                        num(front.width) + " mm) would be " +
                        (margin < 0 ? "off the panel or cut its edge"
                                    : "only " + num(margin) + " mm from the panel edge") +
                        "; at least " + num(cfg.minEdgeMaterial) + " mm of material is required for the " +
                        num(spacing) + " mm hole spacing.");
        }
        w.add(front, domain::Hole{cfg.face, fc.u, fc.v, d, 0.0, true}, what + ": fixing hole");
    }
    domain::HardwareInstance inst;
    inst.id = cfg.instanceId;
    inst.definition = handle.id;
    inst.quantity = 1;
    inst.parts = {front.id};
    inst.position = point(hc, zc);
    inst.note = std::string(horizontal ? "Horizontal" : "Vertical") + " handle on " + front.name;
    Placement out;
    out.instances = {inst};
    out.operations = w.take();
    return out;
}

} // namespace fos::hardware
