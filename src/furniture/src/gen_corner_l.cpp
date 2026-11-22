// SPDX-License-Identifier: AGPL-3.0-only
/// L-shaped (90 degree) corner base cabinet (README "Corner cabinets").
///
/// FRAME (assembly, ARCHITECTURE.md 4.2): leg A runs along the rear wall, x in [0, width],
/// y in [0, depth] with its front at y = 0. Leg B runs along the left wall (x = 0), x in
/// [0, corner_depth_b], from the rear edge y = depth towards the room down to
/// yF = depth - corner_width_b (< 0). The inside corner of the fronts is (corner_depth_b, 0).
#include "fos/furniture/contour.hpp"
#include "fos/machining/validation.hpp"
#include "gen_context.hpp"

#include <variant>

namespace fos::furniture::detail {
namespace {

core::Error tooSmall(const Gen& g, const std::string& message) {
    return core::makeError(core::ErrorCode::ConstraintViolation,
                           "L-shaped corner cabinet '" + g.def.id.str() + "': " + message, g.def.id.str());
}

/// Shape of an L-shaped horizontal panel of L x W (part frame): the box minus the notch
/// x > cx, y < cy (the room-side corner). Segments: 0 end at leg B (y = 0), 1 front of leg B
/// (x = cx), 2 front of leg A (y = cy), 3 end at leg A (x = L), 4 rear of leg A, 5 rear of leg
/// B. The two front segments take the band of the part's L1 edge (generator role Visible
/// plus overrides); the others stay raw. Part::edges is cleared.
void makeLShape(domain::Part& part, double cx, double cy) {
    const double L = part.length, W = part.width;
    domain::PartShape shape;
    for (const auto& [x, y] : {std::pair{0.0, 0.0}, {cx, 0.0}, {cx, cy}, {L, cy}, {L, W}, {0.0, W}})
        shape.outer.vertices.push_back({geometry::Vec2{x, y}, 0.0});
    const std::optional<domain::EdgeBandId> front = part.edge(domain::EdgeSide::L1);
    if (front) shape.outerEdges = {std::nullopt, front, front, std::nullopt, std::nullopt, std::nullopt};
    part.edges = {};
    part.custom["furniture.corner"] = "L";
    part.shape = std::move(shape);
}

void corner(Gen& g, std::size_t face, std::size_t edge, JointFastener f, const std::string& faceSlot,
            const std::string& edgeSlot) {
    if (f == JointFastener::None) return;
    Job j;
    j.kind = JobKind::Corner;
    j.instance = "hw.joint." + edgeSlot + "." + faceSlot;
    j.a = face;
    j.b = edge;
    j.fastener = f;
    g.jobs.push_back(std::move(j));
}

void hingeJob(Gen& g, std::size_t door, std::size_t side, const std::string& slot) {
    Job j;
    j.kind = JobKind::Hinges;
    j.instance = "hw.hinges." + slot;
    j.a = door;
    j.b = side;
    j.hinge = g.def.hardware.hinge; // Full overlay only (checkCorner)
    g.jobs.push_back(std::move(j));
}

std::string checkDoor(const char* name, double width, const std::string& formula) {
    if (width >= kMinCornerDoorWidth - core::kLengthEps && width <= kMaxCornerDoorWidth + core::kLengthEps) return {};
    return std::string(name) + " would be " + num(width) + " mm wide (" + formula + "); corner doors must be " +
           num(kMinCornerDoorWidth) + " to " + num(kMaxCornerDoorWidth) + " mm wide.";
}

} // namespace

core::Result<void> buildLCorner(Gen& g) {
    namespace p = param;
    namespace c = corner_param;
    using domain::PartRole;
    const ConstructionOptions& o = g.def.construction;
    const double T = g.v(p::kThickness), WA = g.v(p::kWidth), DA = g.v(p::kDepth);
    const double WB = g.v(c::kWidthB), DB = g.v(c::kDepthB);
    const double yF = DA - WB; // front end of leg B
    if (!(DB > T) || DB > WA - T - core::kLengthEps)
        return tooSmall(g, "corner_depth_b " + num(DB) + " mm must be more than the board thickness " + num(T) +
                               " mm and less than width " + num(WA) + " mm minus the end side (" + num(WA - T) + " mm).");
    if (WB < DA + T + core::kLengthEps)
        return tooSmall(g, "corner_width_b " + num(WB) + " mm must exceed depth " + num(DA) + " mm plus the end side " +
                               num(T) + " mm, otherwise leg B has no opening.");

    const double sideZ = g.v(p::kSideBottom), sideL = g.v(p::kSideHeight);
    const auto role = MaterialRole::Carcass;
    // Side A (end of leg A): like the right side of a cabinet; inner face A at x = width - T.
    const std::size_t sideA =
        g.addPart("side.a", "Side A", PartRole::Side, role, sideL, DA, T, Transform::fromAxes(kUp, kBack, {WA, 0, sideZ}),
                  Edges{EdgeUse::Visible, EdgeUse::Raw, EdgeUse::Hidden, EdgeUse::Hidden});
    // Side B (end of leg B): length up, width -> +X (wall to front, L2 = front edge),
    // thickness -> +Y; inner face A at y = yF + T.
    const std::size_t sideB =
        g.addPart("side.b", "Side B", PartRole::Side, role, sideL, DB, T, Transform::fromAxes(kUp, kRight, {0, yF, sideZ}),
                  Edges{EdgeUse::Raw, EdgeUse::Visible, EdgeUse::Hidden, EdgeUse::Hidden});

    // Bottom / top: L-shaped, between the end sides, up to the wall lines x = 0 and y = depth.
    const double panelL = WA - T, panelW = DA - (yF + T), notchY = -(yF + T);
    const Edges frontBand{EdgeUse::Visible, EdgeUse::Raw, EdgeUse::Raw, EdgeUse::Raw};
    const std::size_t bottom = g.addPart("bottom", "Bottom", PartRole::Bottom, role, panelL, panelW, T,
                                         Transform::translation({0, yF + T, g.v(p::kCarcassBottom)}), frontBand);
    const std::size_t top = g.addPart("top", "Top", PartRole::Top, role, panelL, panelW, T,
                                      Transform::translation({0, yF + T, g.v(p::kInteriorTop)}), frontBand);
    makeLShape(g.assembly.parts[bottom], DB, notchY);
    makeLShape(g.assembly.parts[top], DB, notchY);
    for (const auto& [side, slot] : {std::pair{sideA, "side.a"}, std::pair{sideB, "side.b"}}) {
        corner(g, side, bottom, o.bottomFastener, slot, "bottom");
        corner(g, side, top, o.topFastener, slot, "top");
    }

    if (o.plinth == PlinthKind::SidesToFloor) {
        // Two recessed plinth panels: A (length -> +X, thickness -> -Y) and B (length -> +Y,
        // thickness -> +X); A runs past B's end so the inner corner is closed.
        const double s = g.v(p::kPlinthSetback), ph = g.v(p::kPlinthHeight);
        if (DB - s - T <= core::kLengthEps)
            return tooSmall(g, "plinth setback " + num(s) + " mm leaves no room for plinth B in leg B (corner_depth_b " +
                                   num(DB) + " mm).");
        const std::size_t plA = g.addPart("plinth.a", "Plinth A", PartRole::Plinth, role, WA - DB + s, ph, T,
                                          Transform::fromAxes(kRight, kUp, {DB - s - T, s + T, 0}), Edges{});
        const std::size_t plB = g.addPart("plinth.b", "Plinth B", PartRole::Plinth, role, s - yF - T, ph, T,
                                          Transform::fromAxes(kBack, kUp, {DB - s - T, yF + T, 0}), Edges{});
        corner(g, sideA, plA, o.dividerFastener, "side.a", "plinth.a");
        corner(g, sideB, plB, o.dividerFastener, "side.b", "plinth.b");
    }

    // Adjustable L-shaped shelves: n shelves leave n + 1 equal clear spaces; shelf_clearance
    // from both end sides, front edges shelf_setback behind both fronts, rear edges on the wall lines.
    const int n = g.def.interior.shelfCount;
    if (n > 0) {
        const double clr = g.v(p::kShelfClearance), sb = g.v(p::kShelfSetback);
        const double z0 = g.v(p::kInteriorBottom), height = g.v(p::kInteriorTop) - z0;
        const double space = (height - n * T) / (n + 1);
        if (space <= core::kLengthEps)
            return tooSmall(g, "the interior is " + num(height) + " mm high; " + std::to_string(n) + " shelves of " +
                                   num(T) + " mm do not fit. Reduce the shelf count.");
        const double y0 = yF + T + clr, len = WA - T - clr, wid = DA - y0;
        if (DB - sb <= core::kLengthEps || sb - y0 <= core::kLengthEps || sb >= DA)
            return tooSmall(g, "shelf setback " + num(sb) + " mm leaves no L-shaped shelf.");
        for (int i = 1; i <= n; ++i) {
            const std::string slot = joinSlot({"shelf", std::to_string(i)});
            const std::size_t shelf =
                g.addPart(slot, "Shelf", PartRole::Shelf, role, len, wid, T,
                          Transform::translation({0, y0, z0 + i * space + (i - 1) * T}), frontBand);
            makeLShape(g.assembly.parts[shelf], DB - sb, sb - y0);
            g.shelfCarriers.emplace_back(shelf, sideA);
            g.shelfCarriers.emplace_back(shelf, sideB);
        }
    }

    if (o.back == BackMount::Overlay) {
        // Back A on the rear wall (thickness -> -Y, y in [depth, depth + bt]) runs over the
        // end of back B (x in [-bt, 0]); both inset back_inset from the end sides' outer faces.
        const double bt = g.v(p::kBackThickness), inset = g.v(p::kBackInset);
        const double bz = g.v(p::kBackZ), bh = g.v(p::kBackHeight);
        g.addPart("back.a", "Back panel A", PartRole::Back, MaterialRole::Back, WA - inset + bt, bh, bt,
                  Transform::fromAxes(kRight, kUp, {-bt, DA + bt, bz}), Edges{});
        g.addPart("back.b", "Back panel B", PartRole::Back, MaterialRole::Back, WB - inset, bh, bt,
                  Transform::fromAxes(kBack, kUp, {-bt, yF + inset, bz}), Edges{});
    }

    if (!g.def.interior.front) return {};
    // Two independent full-overlay doors meeting at the inside corner (bi-fold hinges are not
    // modelled). Door A in the plane of leg A (inner face y = -front_offset), starting one
    // front_gap beyond door B's outer face; door B in the plane of leg B (inner face
    // x = corner_depth_b + front_offset), ending flush with door A's outer face.
    const FrontSpec& spec = *g.def.interior.front;
    const double ft = g.v(p::kFrontThickness), off = g.v(p::kFrontOffset), gap = g.v(p::kFrontGap);
    const double reveal = g.v(p::kFrontReveal), oo = g.v(p::kOverlayOuter);
    const double z0 = g.v(p::kInteriorBottom) - oo, z1 = g.v(p::kInteriorTop) + oo;
    const double xa0 = DB + off + ft + gap, xa1 = WA - reveal;
    const double yb0 = yF + T - oo, yb1 = -off - ft;
    std::string why = checkDoor("door A (leg A)", xa1 - xa0,
                                "width - front reveal - (corner_depth_b + front offset + front thickness + front gap)");
    if (why.empty())
        why = checkDoor("door B (leg B)", yb1 - yb0,
                        "corner_width_b - depth - front reveal - front offset - front thickness");
    if (!why.empty()) return tooSmall(g, why + " Change width, corner_width_b or corner_depth_b.");
    const Edges all{EdgeUse::Front, EdgeUse::Front, EdgeUse::Front, EdgeUse::Front};
    // Door A: length -> +Z, width -> +X, thickness -> +Y (face A inner). Hinged on side A (right).
    const std::size_t doorA = g.addPart("door.a", "Door A", PartRole::Door, MaterialRole::Front, z1 - z0, xa1 - xa0, ft,
                                        Transform::fromAxes(kUp, kRight, {xa0, yb1, z0}), all);
    // Door B: length -> +Z, width -> +Y, thickness -> -X (face A inner at x = DB + off).
    // Hinged on side B (smaller y = its left end in handle terms).
    const std::size_t doorB = g.addPart("door.b", "Door B", PartRole::Door, MaterialRole::Front, z1 - z0, yb1 - yb0, ft,
                                        Transform::fromAxes(kUp, kBack, {DB + off + ft, yb0, z0}), all);
    if (spec.hinges) {
        hingeJob(g, doorA, sideA, "door.a");
        hingeJob(g, doorB, sideB, "door.b");
    }
    handleJob(g, doorA, "door.a", spec, true, false);
    handleJob(g, doorB, "door.b", spec, true, true);
    return {};
}

void placeLCornerShelfPins(Gen& g) {
    namespace p = param;
    if (!g.def.construction.shelfSupports || g.def.interior.shelfCount <= 0) return;
    const std::size_t sideA = g.index("side.a"), sideB = g.index("side.b");
    auto& parts = g.assembly.parts;
    const domain::HardwareDefinition* pin = g.libs.findHardware(g.def.hardware.shelfPin);
    if (!pin) {
        g.error("furniture.hardware.missing_definition",
                "The shelf support '" + g.def.hardware.shelfPin.str() + "' selected for cabinet '" + g.def.id.str() +
                    "' is not in the hardware library; no shelf pins were drilled.",
                g.def.id.str());
        return;
    }
    const auto d = pin->params.find(hardware::keys::kHoleDiameter), dep = pin->params.find(hardware::keys::kHoleDepth);
    if (d == pin->params.end() || dep == pin->params.end()) {
        g.error("furniture.hardware.placement_failed",
                "The shelf support '" + pin->id.str() + "' has no hole_diameter / hole_depth; no shelf pins were drilled.",
                g.def.id.str());
        return;
    }
    const double dia = d->second, depth = dep->second, T = g.v(p::kThickness);
    if (depth >= T - core::kLengthEps) {
        g.error("furniture.hardware.placement_failed",
                "Shelf pin holes of " + num(depth) + " mm would pass through the " + num(T) +
                    " mm end sides; no shelf pins were drilled.",
                g.def.id.str());
        return;
    }
    const double sideZ = g.v(p::kSideBottom), sb = g.v(p::kShelfSetback), DA = g.v(p::kDepth);
    const double DB = g.v(corner_param::kDepthB);
    const double front = g.v(p::kShelfPinFront), back = g.v(p::kShelfPinBack);
    for (int i = 1; i <= g.def.interior.shelfCount; ++i) {
        const std::string slot = joinSlot({"shelf", std::to_string(i)});
        const std::size_t shelf = g.index(slot);
        if (shelf >= parts.size()) continue;
        const std::string instance = g.partId("hw.pins." + slot);
        // Pin centre height = shelf underside - hole diameter / 2 (as hardware::placeShelfSupports).
        const double u = parts[shelf].bounds().min.z - dia / 2 - sideZ;
        // Side A face A: v = assembly y; side B face A: v = assembly x.
        const std::pair<std::size_t, double> holes[] = {
            {sideA, sb + front}, {sideA, DA - back}, {sideB, DB - sb - front}, {sideB, back}};
        std::vector<domain::Operation> ops;
        std::string collision;
        for (std::size_t k = 0; k < 4; ++k) {
            const auto& [side, v] = holes[k];
            domain::Operation op;
            op.id = domain::OperationId(instance + "/op." + std::to_string(k + 1));
            const domain::Hole h{domain::Face::A, u, v, dia, depth, false};
            op.shape = h;
            op.source = instance;
            op.note = "Shelf pin hole";
            for (const auto& existing : parts[side].operations)
                if (const auto* e = std::get_if<domain::Hole>(&existing.shape);
                    e && collision.empty() && machining::holesCollide(parts[side], *e, h))
                    collision = "its hole at (" + num(u) + ", " + num(v) + ") on face A of '" + parts[side].name +
                                "' would intersect '" + existing.id.str() + "'";
            ops.push_back(std::move(op));
        }
        if (!collision.empty()) {
            g.warn("furniture.shelf_pins.dropped",
                   "The shelf pins of '" + parts[shelf].name + "' (" + parts[shelf].id.str() + ") were not drilled: " +
                       collision + ". The shelf has no supports; change the shelf count.",
                   parts[shelf].id.str());
            continue;
        }
        for (std::size_t k = 0; k < 4; ++k) parts[k < 2 ? sideA : sideB].operations.push_back(std::move(ops[k]));
        domain::HardwareInstance inst;
        inst.id = domain::HardwareInstanceId(instance);
        inst.definition = pin->id;
        inst.quantity = 4;
        inst.parts = {parts[sideA].id, parts[sideB].id, parts[shelf].id};
        inst.position = parts[sideA].placement.applyPoint(geometry::Vec3{u, sb + front, T});
        inst.note = "Two pins in each end side of the L-shaped shelf";
        g.assembly.hardware.push_back(std::move(inst));
    }
}

void checkLCornerOutlines(Gen& g) {
    for (const auto& part : g.assembly.parts) {
        const auto it = part.custom.find("furniture.corner");
        if (it == part.custom.end()) continue;
        const auto ov = g.def.materials.overrides.find(part.custom.at("furniture.slot"));
        if (ov != g.def.materials.overrides.end() && ov->second.contour) continue; // checked by applyContours
        auto found = checkOperationsInsideOutline(part);
        g.diagnostics.insert(g.diagnostics.end(), found.begin(), found.end());
    }
}

} // namespace fos::furniture::detail
