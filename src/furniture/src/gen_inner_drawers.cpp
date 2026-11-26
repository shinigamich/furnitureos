// SPDX-License-Identifier: AGPL-3.0-only
/// Inner drawers behind doors (README "Inner drawers"): drawer boxes from the drawer-box
/// generator, optional facias, packer strips on hinge-carrying panels and the slide jobs.
#include "gen_context.hpp"

#include <algorithm>
#include <cmath>

namespace fos::furniture::detail {
namespace {

/// Optional parameter names (README "Inner drawers") with their GENERIC defaults, mm.
struct OptionalParam {
    const char* name;
    double fallback;
};
/// Free depth between the inner face of the closed door and the front of the drawer (facia
/// or box front): handle screws, hinge cups and bumpers behind the door.
constexpr OptionalParam kDoorClearance{"inner_drawer_door_clearance", 20};
/// How far the hinges (arm and mounting plate) and the opened door reach into the clear
/// opening past the inner face of a panel that carries the door's hinges.
constexpr OptionalParam kHingeProjection{"inner_drawer_hinge_projection", 16};
/// Height of one packer strip (from the underside of its drawer box upwards).
constexpr OptionalParam kPackerHeight{"inner_drawer_packer_height", 60};
/// Gap around a facia (top / bottom of its compartment, carrying faces left / right).
constexpr OptionalParam kFaciaGap{"inner_drawer_facia_gap", 3};
/// Grip cut-out of a facia: width, height and distance of its top edge below the facia top.
constexpr OptionalParam kGripWidth{"inner_drawer_grip_width", 100};
constexpr OptionalParam kGripHeight{"inner_drawer_grip_height", 30};
constexpr OptionalParam kGripTop{"inner_drawer_grip_top_offset", 20};
/// Box front setback of drawer boxes (shared with drawer stacks, gen_drawers.cpp).
constexpr OptionalParam kFrontSetback{"drawer_box_front_setback", 0};

/// Least material left around a grip cut-out, mm (as for sliding-door grips).
constexpr double kGripMargin = 5.0;
/// More packer layers than this are refused (the hinge projection is unreasonable).
constexpr int kMaxPackerLayers = 3;

} // namespace

core::Result<void> buildInnerDrawers(Gen& g, const InnerDrawerSpec& spec, const std::string& path, const Box& box,
                                     const Bounds& b, const Cover& cover) {
    namespace p = param;
    const std::string label = "Inner drawers of section '" + sectionLabel(path) + "' of cabinet '" + g.def.id.str() + "'";
    const auto fail = [&](core::ErrorCode code, const std::string& why) {
        return core::makeError(code, label + ": " + why, g.def.id.str());
    };
    const FrontSpec& door = *cover.front; // checkTree guarantees a Door / DoorPair / SlidingDoors cover
    const bool sliding = door.kind == FrontKind::SlidingDoors;

    const domain::HardwareDefinition* slide = g.libs.findHardware(spec.slide);
    if (!slide) return fail(core::ErrorCode::NotFound, "the drawer slide '" + spec.slide.str() + "' is not in the hardware library.");
    if (slide->kind != domain::HardwareKind::DrawerSlide)
        return fail(core::ErrorCode::InvalidArgument,
                    "'" + spec.slide.str() + "' is not a drawer slide (kind DrawerSlide); choose a drawer slide.");

    std::map<std::string, double> o;
    for (const OptionalParam& op : {kDoorClearance, kHingeProjection, kPackerHeight, kFaciaGap, kGripWidth, kGripHeight,
                                    kGripTop, kFrontSetback}) {
        const double v = g.opt(op.name, op.fallback);
        if (!std::isfinite(v) || v < 0)
            return fail(core::ErrorCode::ConstraintViolation,
                        "parameter '" + std::string(op.name) + "' is " + num(v) + " mm; it must be zero or positive.");
        o[op.name] = v;
    }

    // Facia board: the drawer material (MaterialAssignment::drawer).
    const bool facia = spec.front == InnerDrawerFront::Facia;
    double tf = 0;
    if (facia) {
        const domain::MaterialId id = roleMaterial(g.def.materials, MaterialRole::Drawer);
        const domain::Material* m = g.libs.findMaterial(id);
        if (!m)
            return fail(core::ErrorCode::NotFound, "the drawer material '" + id.str() + "' is not in the material library.");
        tf = m->thickness;
    }

    // DOOR CLEARANCE RULE: the drawer front (facia or box front) stays inner_drawer_door_clearance
    // behind the inner face of the closed door: -front_offset for overlay doors, front_thickness
    // for inset doors. Behind sliding doors it starts at the usable interior front (behind the
    // tracks and their interior clearance).
    double yFront = box.y0;
    if (!sliding) {
        const double yDoor = door.overlay == OverlayKind::Inset ? g.v(p::kFrontThickness) : -g.v(p::kFrontOffset);
        yFront = std::max(box.y0, yDoor + o[kDoorClearance.name]);
    }
    const double y0 = yFront + tf + o[kFrontSetback.name]; // front face of the box sides

    // PACKER RULE: a bounding panel of this section that carries the hinges of the covering
    // door gets ceil(inner_drawer_hinge_projection / thickness) packer strips (carcass board),
    // so that the slide's carrying face stands at least the hinge projection inside the panel.
    const bool hingedLeft = !sliding && door.hinges &&
                            (door.kind == FrontKind::DoorPair || door.hingeSide == HingeSide::Left);
    const bool hingedRight = !sliding && door.hinges &&
                             (door.kind == FrontKind::DoorPair || door.hingeSide == HingeSide::Right);
    const double T = g.v(p::kThickness), proj = o[kHingeProjection.name];
    const auto layers = [&](bool packed) {
        return packed && proj > core::kLengthEps ? static_cast<int>(std::ceil(proj / T - 1e-9)) : 0;
    };
    const int nL = layers(hingedLeft && b.left == cover.bounds.left);
    const int nR = layers(hingedRight && b.right == cover.bounds.right);
    if (std::max(nL, nR) > kMaxPackerLayers)
        return fail(core::ErrorCode::ConstraintViolation,
                    "a hinge projection of " + num(proj) + " mm needs " + std::to_string(std::max(nL, nR)) + " packers of " +
                        num(T) + " mm per side; at most " + std::to_string(kMaxPackerLayers) +
                        " are generated. Check 'inner_drawer_hinge_projection'.");
    const double cx0 = box.x0 + nL * T, cx1 = box.x1 - nR * T;
    if (cx1 - cx0 <= core::kLengthEps)
        return fail(core::ErrorCode::ConstraintViolation,
                    "the section is " + num(box.x1 - box.x0) + " mm wide; the packers (" + std::to_string(nL + nR) +
                        " x " + num(T) + " mm) leave no room for a drawer.");

    const double height = box.z1 - box.z0;
    auto comps = distribute(g, height, spec.heights, label + " (clear height " + num(height) + " mm)");
    if (!comps) return fail(comps.error().code, comps.error().message);

    const DrawerBoxSpec ds{spec.slide, spec.bottom, spec.joint};
    double cz0 = box.z0;
    for (std::size_t i = 0; i < spec.heights.size(); ++i) {
        const double cz1 = cz0 + comps.value()[i];
        const std::string base = joinSlot({"inner_drawer", path, std::to_string(i + 1)});
        const std::string who = "drawer " + std::to_string(i + 1);
        DrawerBoxSite site;
        site.y0 = y0;
        site.x0 = cx0;
        site.x1 = cx1;
        site.zMin = box.z0;
        site.zMax = box.z1;
        auto boxParts = addDrawerBoxParts(g, ds, base, site, cz0, cz1);
        if (!boxParts)
            return fail(boxParts.error().code,
                        (sliding ? "behind the sliding doors (usable interior from y = " + num(box.y0) + " mm), "
                                 : std::string()) +
                            boxParts.error().message);
        const DrawerBoxParts& bp = boxParts.value();

        // Packers: length -> +Y (the slide length from the box front), width -> +Z, thickness -> +X.
        const double ph = std::min(o[kPackerHeight.name], cz1 - bp.zLow);
        if (nL + nR > 0 && ph <= core::kLengthEps)
            return fail(core::ErrorCode::ConstraintViolation,
                        who + ": parameter 'inner_drawer_packer_height' is " + num(o[kPackerHeight.name]) +
                            " mm; the packers need a positive height.");
        std::size_t leftCarrier = b.left, rightCarrier = b.right;
        const Edges raw{EdgeUse::Raw, EdgeUse::Raw, EdgeUse::Raw, EdgeUse::Raw};
        const auto addPacker = [&](bool left, int k) {
            const double x = left ? box.x0 + k * T : box.x1 - (k + 1) * T;
            const std::string slot = base + ".packer." + (left ? "left." : "right.") + std::to_string(k + 1);
            const std::size_t idx =
                g.addPart(slot, std::string("Inner drawer packer ") + (left ? "left" : "right"), domain::PartRole::Generic,
                          MaterialRole::Carcass, bp.slideLength, ph, T, Transform::fromAxes(kBack, kUp, {x, y0, bp.zLow}),
                          raw);
            if (k == 0) {
                const std::size_t panel = left ? b.left : b.right;
                g.packers.emplace_back(idx, panel);
                // Intermediate hinges of the door are moved clear of the packer like of a shelf.
                g.shelfCarriers.emplace_back(idx, panel);
            }
            return idx;
        };
        for (int k = 0; k < nL; ++k) leftCarrier = addPacker(true, k);
        for (int k = 0; k < nR; ++k) rightCarrier = addPacker(false, k);

        if (facia) {
            const double gap = o[kFaciaGap.name];
            const double fx0 = cx0 + gap, fx1 = cx1 - gap, fz0 = cz0 + gap, fz1 = cz1 - gap;
            if (fx1 - fx0 <= core::kLengthEps || fz1 - fz0 <= core::kLengthEps)
                return fail(core::ErrorCode::ConstraintViolation,
                            who + ": a facia gap of " + num(gap) + " mm leaves no facia in the compartment of " +
                                num(cx1 - cx0) + " x " + num(cz1 - cz0) + " mm.");
            // Like a drawer front: length -> +X, width -> -Z (from the top edge down),
            // thickness -> +Y; face B (z = 0) is the visible face at y = y0 - tf.
            const std::string slot = base + ".facia";
            const std::size_t fi = g.addPart(
                slot, "Inner drawer facia " + std::to_string(i + 1), domain::PartRole::DrawerFront, MaterialRole::Drawer,
                fx1 - fx0, fz1 - fz0, tf, Transform::fromAxes(kRight, kDown, {fx0, y0 - tf, fz1}),
                Edges{EdgeUse::Front, EdgeUse::Front, EdgeUse::Front, EdgeUse::Front});
            if (spec.handle == InnerFaciaHandle::CutOut) {
                const double L = fx1 - fx0, W = fz1 - fz0;
                const double gw = o[kGripWidth.name], gh = o[kGripHeight.name], top = o[kGripTop.name];
                if (!(gw > 0) || !(gh > 0) || gw > L - 2 * kGripMargin + core::kLengthEps ||
                    top < kGripMargin - core::kLengthEps || top + gh > W - kGripMargin + core::kLengthEps)
                    return fail(core::ErrorCode::ConstraintViolation,
                                who + ": the grip cut-out of " + num(gw) + " x " + num(gh) + " mm, " + num(top) +
                                    " mm below the top edge, does not fit on the facia of " + num(L) + " x " + num(W) +
                                    " mm with " + num(kGripMargin) + " mm of material at every edge.");
                domain::Pocket pocket;
                pocket.face = domain::Face::B;
                pocket.depth = tf;
                pocket.through = true;
                const double u0 = L / 2 - gw / 2, u1 = L / 2 + gw / 2, v0 = top, v1 = top + gh;
                pocket.contour = {{u0, v0}, {u1, v0}, {u1, v1}, {u0, v1}}; // counter-clockwise in (u, v)
                domain::Part& fp = g.assembly.parts[fi];
                domain::Operation op;
                op.id = domain::OperationId(fp.id.str() + "/op.grip");
                op.shape = std::move(pocket);
                op.source = g.partId("rule.inner_drawer_grip");
                op.note = "Grip cut-out";
                fp.operations.push_back(std::move(op));
            }
        }
        addSlideJobs(g, ds, base, bp, leftCarrier, nL == 0 && !b.leftOuter, rightCarrier, nR == 0 && !b.rightOuter);
        cz0 = cz1;
    }
    return {};
}

void checkInnerDrawerHinges(Gen& g) {
    constexpr OptionalParam kPlateMargin{"inner_drawer_hinge_plate_margin", 20};
    const double margin = std::max(0.0, g.opt(kPlateMargin.name, kPlateMargin.fallback));
    const std::string hingePrefix = g.partId("hw.hinges.");
    for (const auto& [packerIdx, panelIdx] : g.packers) {
        const domain::Part& packer = g.assembly.parts[packerIdx];
        const domain::Part& panel = g.assembly.parts[panelIdx];
        const geometry::Aabb k = packer.bounds();
        const geometry::Aabb pb = panel.bounds();
        // The face of the panel the packer is fixed to (the packer touches it).
        const double contactX = std::abs(k.min.x - pb.max.x) < std::abs(k.max.x - pb.min.x) ? pb.max.x : pb.min.x;
        std::string reported;
        for (const auto& op : panel.operations) {
            if (op.source.rfind(hingePrefix, 0) != 0 || op.source == reported) continue;
            const auto* hole = std::get_if<domain::Hole>(&op.shape);
            if (!hole) continue;
            const geometry::Vec3 c = machining::faceToAssembly(panel, hole->face, hole->u, hole->v);
            if (std::abs(c.x - contactX) > 0.5) continue; // a hole in the other face of the panel
            if (c.y < k.min.y - margin || c.y > k.max.y + margin || c.z < k.min.z - margin || c.z > k.max.z + margin)
                continue;
            reported = op.source;
            g.error("furniture.inner_drawers.hinge_conflict",
                    "The mounting plate of hinge '" + op.source.substr(g.def.id.str().size() + 1) + "' on '" + panel.name +
                        "' (plate hole at y " + num(c.y) + ", z " + num(c.z) + ") lies within " + num(margin) +
                        " mm of the inner-drawer packer '" + packer.custom.at("furniture.slot") + "' (y " + num(k.min.y) +
                        " to " + num(k.max.y) + ", z " + num(k.min.z) + " to " + num(k.max.z) +
                        "). Change the drawer heights or 'hinge_offset' so the hinge clears the packer.",
                    packer.id.str());
        }
    }
}

} // namespace fos::furniture::detail
