// SPDX-License-Identifier: AGPL-3.0-only
/// Drawer boxes behind drawer fronts: two sides, inner front, back and bottom, carried by a
/// pair of slides (hardware::placeDrawerSlides, scheduled as Slide jobs).
#include "gen_context.hpp"

#include <algorithm>
#include <cmath>

namespace fos::furniture::detail {
namespace {

/// Optional parameter names (README "Drawer boxes") with their defaults, mm.
struct OptionalParam {
    const char* name;
    double fallback;
};
constexpr OptionalParam kTopClearance{"drawer_box_top_clearance", 25};
constexpr OptionalParam kBottomClearance{"drawer_box_bottom_clearance", 15};
constexpr OptionalParam kRearClearance{"drawer_box_rear_clearance", 10};
constexpr OptionalParam kFrontSetback{"drawer_box_front_setback", 0};
constexpr OptionalParam kGrooveOffset{"drawer_bottom_groove_offset", 10};
constexpr OptionalParam kGrooveDepth{"drawer_bottom_groove_depth", 6};
constexpr OptionalParam kBottomPlay{"drawer_bottom_clearance", 1};
constexpr OptionalParam kJointGrooveClearance{"drawer_joint_groove_clearance", 5};

void addBottomGroove(Gen& g, std::size_t i, domain::Face face, double v, double width, double depth) {
    domain::Part& part = g.assembly.parts[i];
    domain::Operation op;
    op.id = domain::OperationId(part.id.str() + "/op.groove.drawer_bottom");
    op.shape = domain::Groove{face, 0.0, v, part.length, v, width, depth};
    op.source = g.partId("rule.drawer_bottom_groove");
    op.note = "Groove for the drawer bottom";
    part.operations.push_back(std::move(op));
}

/// Largest hole radius (mm) the corner fastener `kind` of the cabinet's hardware selection
/// drills (dowel: diameter / 2; confirmat: max(through, pilot) / 2); 0 when the definition
/// is missing (the joint job then reports furniture.hardware.missing_definition).
double fastenerRadius(const Gen& g, JointFastener kind) {
    const domain::HardwareDefinition* d =
        g.libs.findHardware(kind == JointFastener::Confirmat ? g.def.hardware.confirmat : g.def.hardware.dowel);
    if (!d) return 0;
    double r = 0;
    for (const char* key : {hardware::keys::kDiameter, hardware::keys::kThroughDiameter, hardware::keys::kPilotDiameter})
        if (const auto it = d->params.find(key); it != d->params.end()) r = std::max(r, it->second / 2);
    return r;
}

} // namespace

core::Result<void> buildDrawerBox(Gen& g, const FrontSpec& spec, const DrawerBoxSpec& ds, const std::string& frontSlot,
                                  double fz0, double fz1, const Box& box, const Bounds& b) {
    namespace p = param;
    const std::string owner = g.partId(frontSlot);
    const std::string label = "Drawer box behind '" + frontSlot + "' of cabinet '" + g.def.id.str() + "'";
    const auto fail = [&](const std::string& why) {
        return core::makeError(core::ErrorCode::ConstraintViolation, label + ": " + why, owner);
    };

    const domain::HardwareDefinition* slide = g.libs.findHardware(ds.slide);
    if (!slide)
        return core::makeError(core::ErrorCode::NotFound,
                               label + ": the drawer slide '" + ds.slide.str() + "' is not in the hardware library.", owner);
    const auto thickness = [&](MaterialRole role, const char* what) -> core::Result<double> {
        const domain::MaterialId id = roleMaterial(g.def.materials, role);
        const domain::Material* m = g.libs.findMaterial(id);
        if (!m)
            return core::makeError(core::ErrorCode::NotFound,
                                   label + ": the " + std::string(what) + " material '" + id.str() +
                                       "' is not in the material library.",
                                   owner);
        return m->thickness;
    };
    const auto tSide = thickness(MaterialRole::Drawer, "drawer");
    if (!tSide) return tSide.error();
    const auto tBottom = thickness(MaterialRole::DrawerBottom, "drawer bottom");
    if (!tBottom) return tBottom.error();
    const double t = tSide.value(), tb = tBottom.value();

    std::map<std::string, double> o;
    for (const OptionalParam& op : {kTopClearance, kBottomClearance, kRearClearance, kFrontSetback, kGrooveOffset,
                                    kGrooveDepth, kBottomPlay, kJointGrooveClearance}) {
        const double v = g.opt(op.name, op.fallback);
        if (!std::isfinite(v) || v < 0) return fail("parameter '" + std::string(op.name) + "' is " + num(v) +
                                                    " mm; it must be zero or positive.");
        o[op.name] = v;
    }
    const bool grooved = ds.bottom == DrawerBottomMount::Grooved;
    const double gDepth = o[kGrooveDepth.name], play = o[kBottomPlay.name];
    if (grooved && (!(gDepth > 0) || gDepth >= t - core::kLengthEps || play >= gDepth))
        return fail("a bottom groove of " + num(gDepth) + " mm (bottom play " + num(play) +
                    " mm) needs 0 < play < groove depth < the " + num(t) + " mm box sides.");

    // Depth: the box starts behind the front plane of the opening (inset fronts occupy y in
    // [0, front_thickness]); the slide is the longest one that fits in front of the back.
    const bool inset = spec.overlay == OverlayKind::Inset;
    const double y0 = (inset ? g.v(p::kFrontThickness) : 0.0) + o[kFrontSetback.name];
    const double available = g.v(p::kInteriorDepth) - y0 - o[kRearClearance.name];
    auto slideLength = hardware::selectSlideLength(*slide, available);
    if (!slideLength)
        return fail(slideLength.error().message + " (interior depth " + num(g.v(p::kInteriorDepth)) +
                    " mm, box front at " + num(y0) + " mm, rear clearance " + num(o[kRearClearance.name]) + " mm)");
    auto boxLength = hardware::drawerBoxLength(*slide, slideLength.value());
    if (!boxLength) return fail(boxLength.error().message);
    auto boxWidth = hardware::drawerBoxOuterWidth(*slide, box.x1 - box.x0, t);
    if (!boxWidth) return fail(boxWidth.error().message);
    auto recess = hardware::requireParam(*slide, hardware::keys::kBottomRecess, true);
    if (!recess) return fail(recess.error().message);
    auto minHeight = hardware::requireParam(*slide, hardware::keys::kMinBoxHeight);
    if (!minHeight) return fail(minHeight.error().message);
    const double len = boxLength.value(), W = boxWidth.value();
    if (len <= 2 * t + core::kLengthEps)
        return fail("a box of " + num(len) + " mm length is not longer than its " + num(t) + " mm front and back.");

    // Height: between the front and the opening, minus the top and bottom clearances.
    const double zLow = std::max(fz0, box.z0) + o[kBottomClearance.name];
    const double zHigh = std::min(fz1, box.z1) - o[kTopClearance.name];
    const double H = zHigh - zLow;
    if (H < minHeight.value() - core::kLengthEps)
        return fail("the box would be " + num(H) + " mm high (front " + num(fz0) + " to " + num(fz1) + ", opening " +
                    num(box.z0) + " to " + num(box.z1) + ", clearances " + num(o[kBottomClearance.name]) + " + " +
                    num(o[kTopClearance.name]) + " mm) but drawer slide '" + slide->id.str() + "' needs at least " +
                    num(minHeight.value()) + " mm. Make the drawer front higher.");
    const double bottomRise = grooved ? std::max(o[kGrooveOffset.name], recess.value()) : 0.0;
    if (!grooved && recess.value() > 0)
        return fail("drawer slide '" + slide->id.str() + "' needs the bottom raised by " + num(recess.value()) +
                    " mm; use a grooved bottom (DrawerBottomMount::Grooved).");
    if (bottomRise + tb >= H - core::kLengthEps)
        return fail("a bottom " + num(bottomRise) + " mm above the box underside does not fit into a box of " + num(H) +
                    " mm height.");

    const double x0 = (box.x0 + box.x1) / 2 - W / 2, x1 = x0 + W;
    const double sideZ = grooved ? zLow : zLow + tb, sideH = grooved ? H : H - tb;
    const Edges topVisible{EdgeUse::Raw, EdgeUse::Visible, EdgeUse::Raw, EdgeUse::Raw}; // L2 = upper edge
    const Edges raw{EdgeUse::Raw, EdgeUse::Raw, EdgeUse::Raw, EdgeUse::Raw};
    const auto slot = [&](const char* s) { return frontSlot + "." + s; };
    const auto mat = MaterialRole::Drawer;
    using domain::PartRole;
    // Sides: length -> +Y (depth), width -> +Z, thickness -> +X. Left inner face A, right inner face B.
    const std::size_t left = g.addPart(slot("side.left"), "Drawer box left side", PartRole::DrawerSide, mat, len, sideH,
                                       t, Transform::fromAxes(kBack, kUp, {x0, y0, sideZ}), topVisible);
    const std::size_t right = g.addPart(slot("side.right"), "Drawer box right side", PartRole::DrawerSide, mat, len,
                                        sideH, t, Transform::fromAxes(kBack, kUp, {x1 - t, y0, sideZ}), topVisible);
    // Inner front and back between the sides: length -> +X, width -> +Z, thickness -> -Y.
    // Inner faces: box front B (z = 0, towards the back), box back A.
    const std::size_t inner = g.addPart(slot("box_front"), "Drawer box front", PartRole::DrawerBack, mat, W - 2 * t,
                                        sideH, t, Transform::fromAxes(kRight, kUp, {x0 + t, y0 + t, sideZ}), topVisible);
    const std::size_t back = g.addPart(slot("box_back"), "Drawer box back", PartRole::DrawerBack, mat, W - 2 * t,
                                       sideH, t, Transform::fromAxes(kRight, kUp, {x0 + t, y0 + len, sideZ}), topVisible);
    if (grooved) {
        const double e = gDepth - play; // engagement of the bottom in each groove
        g.addPart(slot("bottom"), "Drawer box bottom", PartRole::DrawerBottom, MaterialRole::DrawerBottom,
                  W - 2 * t + 2 * e, len - 2 * t + 2 * e, tb,
                  Transform::fromAxes(kRight, kBack, {x0 + t - e, y0 + t - e, zLow + bottomRise}), raw);
        const double width = tb + g.v(p::kGrooveFit), v = bottomRise + tb / 2;
        addBottomGroove(g, left, domain::Face::A, v, width, gDepth);
        addBottomGroove(g, right, domain::Face::B, v, width, gDepth);
        addBottomGroove(g, inner, domain::Face::B, v, width, gDepth);
        addBottomGroove(g, back, domain::Face::A, v, width, gDepth);
    } else {
        g.addPart(slot("bottom"), "Drawer box bottom", PartRole::DrawerBottom, MaterialRole::DrawerBottom, W, len, tb,
                  Transform::fromAxes(kRight, kBack, {x0, y0, zLow}), raw);
    }

    // Corner joints (sides = face parts, inner front / back = edge parts), scheduled before
    // the slides. GROOVE CLEARANCE RULE: with a grooved bottom the fasteners stay in the zone
    // from (upper groove edge + drawer_joint_groove_clearance + fastener hole radius) to the
    // top of the box, so at least the clearance of solid material remains between every
    // joint hole and the groove (the groove runs through all four parts). The joint u
    // coordinate is the edge part's local y = height above the underside of the side.
    if (ds.joint != JointFastener::None) {
        std::optional<machining::Range> zone;
        if (grooved) {
            const double grooveTop = bottomRise + tb + g.v(p::kGrooveFit) / 2;
            const double start = grooveTop + o[kJointGrooveClearance.name] + fastenerRadius(g, ds.joint);
            if (start >= sideH - core::kLengthEps)
                return fail("the corner joints need the zone above " + num(start) +
                            " mm (groove top + clearance + fastener radius) but the box sides are only " + num(sideH) +
                            " mm high. Make the drawer front higher or set the box joint to None.");
            zone = machining::Range{start, sideH};
        }
        const auto jointJob = [&](std::size_t side, std::size_t edge, const std::string& name) {
            Job j;
            j.kind = JobKind::Corner;
            j.instance = "hw.joint." + name;
            j.a = side;
            j.b = edge;
            j.fastener = ds.joint;
            j.zone = zone;
            j.refuseCollisions = true;
            g.jobs.push_back(std::move(j));
        };
        jointJob(left, inner, slot("box_front.left"));
        jointJob(right, inner, slot("box_front.right"));
        jointJob(left, back, slot("box_back.left"));
        jointJob(right, back, slot("box_back.right"));
    }

    const auto slideJob = [&](std::size_t side, std::size_t panel, bool outer, const char* which) {
        Job j;
        j.kind = JobKind::Slide;
        j.instance = "hw.slide." + frontSlot + "." + which;
        j.a = side;
        j.b = panel;
        j.sharedPanel = !outer;
        j.slide = ds.slide;
        j.slideLength = slideLength.value();
        j.refuseCollisions = true;
        g.jobs.push_back(std::move(j));
    };
    slideJob(left, b.left, b.leftOuter, "left");
    slideJob(right, b.right, b.rightOuter, "right");
    return {};
}

} // namespace fos::furniture::detail
