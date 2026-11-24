// SPDX-License-Identifier: AGPL-3.0-only
/// Front pieces: slab fronts and five-piece frame-and-panel fronts (README "Frame-and-panel
/// fronts"): stiles, rails, panel, panel grooves, stub-tenon cheeks or frame dowels.
#include "fos/furniture/limits.hpp"
#include "gen_context.hpp"

#include <algorithm>
#include <cmath>

namespace fos::furniture::detail {
namespace {

/// GENERIC minimum of the visible panel opening (front size minus two stiles / rails), mm.
constexpr double kMinPanelOpening = 50.0;
/// GENERIC minimum wall left on each side of the panel groove (= stub-tenon cheek depth), mm.
constexpr double kMinFrameWall = 2.0;
/// GENERIC: each frame dowel hole is half the dowel length plus this much deep, mm.
constexpr double kDowelHoleExtra = 1.0;
/// GENERIC: the solid rail width (rail width - groove depth) must be at least this many
/// dowel diameters so that both dowels keep clear of the rail edge and of the groove.
constexpr double kDowelZoneDiameters = 4.0;

constexpr const char* kPieceKey = "furniture.front_piece";
constexpr const char* kFrontKey = "furniture.front";

core::Error constraint(const Gen& g, std::string message) {
    return core::makeError(core::ErrorCode::ConstraintViolation, std::move(message), g.def.id.str());
}

void addGroove(Gen& g, std::size_t i, domain::Face face, const std::string& name, double u0, double v0, double u1,
               double v1, double width, double depth, const char* note) {
    domain::Part& part = g.assembly.parts[i];
    domain::Operation op;
    op.id = domain::OperationId(part.id.str() + "/op." + name);
    op.shape = domain::Groove{face, u0, v0, u1, v1, width, depth};
    op.source = g.partId("rule.frame_panel");
    op.note = note;
    part.operations.push_back(std::move(op));
}

domain::Operation hole(const std::string& id, domain::Face face, double u, double v, double diameter, double depth) {
    domain::Operation op;
    op.id = domain::OperationId(id);
    op.shape = domain::Hole{face, u, v, diameter, depth, false};
    op.source = id.substr(0, id.rfind("/op."));
    op.note = "Frame dowel";
    return op;
}

/// Frame dowels of one joint: `stile` (holes in its inner edge `stileFace` at stile x =
/// `su[k]`), `rail` (holes in its end face `railFace` at rail y = `ru[k]`), both centred in
/// the thickness. `at` = first dowel on the contact plane (assembly frame).
core::Result<void> frameDowels(Gen& g, const domain::HardwareDefinition& dowel, const std::string& instance,
                               std::size_t stile, domain::Face stileFace, std::size_t rail, domain::Face railFace,
                               const double (&su)[2], const double (&ru)[2], double diameter, double depth,
                               geometry::Vec3 at) {
    const domain::Part& s = g.assembly.parts[stile];
    const domain::Part& r = g.assembly.parts[rail];
    const std::string id = g.partId(instance);
    hardware::Placement pl;
    domain::HardwareInstance inst;
    inst.id = domain::HardwareInstanceId(id);
    inst.definition = dowel.id;
    inst.quantity = 2;
    inst.parts = {s.id, r.id};
    inst.position = at;
    inst.note = "Frame joint dowels";
    pl.instances.push_back(std::move(inst));
    hardware::PartOperations so{s.id, {}}, ro{r.id, {}};
    for (int k = 0; k < 2; ++k) {
        const std::size_t n = static_cast<std::size_t>(2 * k);
        so.operations.push_back(hole(id + "/op." + std::to_string(n + 1), stileFace, su[k], s.thickness / 2, diameter, depth));
        ro.operations.push_back(hole(id + "/op." + std::to_string(n + 2), railFace, ru[k], r.thickness / 2, diameter, depth));
    }
    pl.operations.push_back(std::move(so));
    pl.operations.push_back(std::move(ro));
    return hardware::applyPlacement(g.assembly, pl);
}

/// Sets the panel material (FramePanelSpec::panelMaterial unless the slot has a material
/// override) and warns when its board thickness differs from the panel thickness.
void panelMaterial(Gen& g, std::size_t i, const FramePanelSpec& fs, const std::string& slot) {
    domain::Part& p = g.assembly.parts[i];
    const auto ov = g.def.materials.overrides.find(slot);
    const bool overridden = ov != g.def.materials.overrides.end() && ov->second.material;
    if (!overridden && fs.panelMaterial) p.material = *fs.panelMaterial; // checkFrontStyle: exists
    const domain::Material* m = g.libs.findMaterial(p.material);
    p.grain = m && m->hasGrain ? domain::GrainDirection::AlongLength : domain::GrainDirection::None;
    if (!overridden && m && !core::approxEqual(m->thickness, fs.panelThickness))
        g.warn("furniture.front.panel_thickness",
               "Panel '" + p.name + "' (" + slot + ") is " + num(fs.panelThickness) + " mm thick but its material '" +
                   m->name + "' is " + num(m->thickness) + " mm; set FramePanelSpec::panelMaterial to a " +
                   num(fs.panelThickness) + " mm board.",
               p.id.str());
}

} // namespace

core::Result<void> checkFrontStyle(const Gen& g, const FrontSpec& spec, const std::string& label) {
    if (spec.style != FrontStyle::FramePanel) return {};
    const std::string who = label + ": the frame-and-panel front";
    if (spec.kind == FrontKind::SlidingDoors || spec.kind == FrontKind::ApplianceNiche ||
        spec.kind == FrontKind::ApplianceFront)
        return core::makeError(core::ErrorCode::Unsupported,
                               label + " has a front of kind " + toString(spec.kind) +
                                   " with style FramePanel; frame-and-panel construction is supported only for Door, "
                                   "DoorPair, Flap and DrawerStack fronts. Use style Slab.",
                               g.def.id.str());
    const FramePanelSpec& f = spec.frame;
    const auto bad = [&](const char* field, double v, bool zeroOk) -> std::string {
        if (!std::isfinite(v) || (zeroOk ? v < 0 : !(v > 0)) || v > limits::kMaxDimension)
            return who + " has " + field + " " + num(v) + " mm; it must be " + (zeroOk ? "at least 0" : "greater than 0") +
                   " and at most " + num(limits::kMaxDimension) + " mm.";
        return {};
    };
    for (const std::string& why : {bad("stile width", f.stileWidth, false), bad("rail width", f.railWidth, false),
                                   bad("panel thickness", f.panelThickness, false),
                                   bad("groove depth", f.grooveDepth, false),
                                   bad("panel clearance", f.panelClearance, true)})
        if (!why.empty()) return core::makeError(core::ErrorCode::InvalidArgument, why, g.def.id.str());
    if (f.panelClearance >= f.grooveDepth)
        return core::makeError(core::ErrorCode::InvalidArgument,
                               who + " has a panel clearance of " + num(f.panelClearance) +
                                   " mm, which is not less than the groove depth of " + num(f.grooveDepth) +
                                   " mm; the panel would not reach into the groove.",
                               g.def.id.str());
    if (f.grooveDepth >= std::min(f.stileWidth, f.railWidth))
        return core::makeError(core::ErrorCode::InvalidArgument,
                               who + " has a groove depth of " + num(f.grooveDepth) +
                                   " mm, which is not less than the stile width (" + num(f.stileWidth) +
                                   " mm) and the rail width (" + num(f.railWidth) + " mm).",
                               g.def.id.str());
    const double ft = g.v(param::kFrontThickness), gw = f.panelThickness + g.v(param::kGrooveFit);
    if ((ft - gw) / 2 < kMinFrameWall - core::kLengthEps)
        return core::makeError(core::ErrorCode::ConstraintViolation,
                               who + " needs a panel groove " + num(gw) + " mm wide (panel thickness " +
                                   num(f.panelThickness) + " mm + groove_fit " + num(g.v(param::kGrooveFit)) +
                                   " mm) in a frame " + num(ft) + " mm thick, leaving " + num((ft - gw) / 2) +
                                   " mm on each side; at least " + num(kMinFrameWall) +
                                   " mm (generic minimum) are needed. Use a thinner panel.",
                               g.def.id.str());
    if (f.panelMaterial && !g.libs.findMaterial(*f.panelMaterial))
        return core::makeError(core::ErrorCode::NotFound,
                               who + " uses the panel material '" + f.panelMaterial->str() +
                                   "', which is not in the material library.",
                               g.def.id.str());
    return {};
}

core::Result<FrontPieces> addFrontPieces(Gen& g, const FrontSpec& spec, const std::string& slot, const std::string& name,
                                         domain::PartRole role, bool horizontalGrain, double x0, double x1, double z0,
                                         double z1, double yInner) {
    const double ft = g.v(param::kFrontThickness), yOuter = yInner - ft;
    const Edges all{EdgeUse::Front, EdgeUse::Front, EdgeUse::Front, EdgeUse::Front};
    const auto mr = MaterialRole::Front;
    FrontPieces out;
    if (spec.style == FrontStyle::Slab) {
        const std::size_t i =
            horizontalGrain
                ? g.addPart(slot, name, role, mr, x1 - x0, z1 - z0, ft, Transform::fromAxes(kRight, kDown, {x0, yOuter, z1}), all)
                : g.addPart(slot, name, role, mr, z1 - z0, x1 - x0, ft, Transform::fromAxes(kUp, kRight, {x0, yOuter, z0}), all);
        out.left = out.right = out.top = out.bottom = out.panel = i;
        return out;
    }

    const FramePanelSpec& f = spec.frame;
    const std::string who = "Frame-and-panel front '" + name + "' (" + slot + ") of cabinet '" + g.def.id.str() + "'";
    const std::string pieces[] = {slot, slot + ".stile.left", slot + ".stile.right", slot + ".rail.top",
                                  slot + ".rail.bottom", slot + ".panel"};
    for (const std::string& s : pieces)
        if (const auto it = g.def.materials.overrides.find(s); it != g.def.materials.overrides.end() && it->second.contour)
            return core::makeError(core::ErrorCode::Unsupported,
                                   who + ": part '" + s + "' has a contour override; contoured frame-and-panel fronts "
                                   "are not supported. Remove the contour or use style Slab.",
                                   g.def.id.str());
    const double W = x1 - x0, H = z1 - z0, sw = f.stileWidth, rw = f.railWidth, d = f.grooveDepth;
    const double openW = W - 2 * sw, openH = H - 2 * rw;
    if (openW < kMinPanelOpening - core::kLengthEps || openH < kMinPanelOpening - core::kLengthEps)
        return constraint(g, who + " measures " + num(W) + " x " + num(H) + " mm (width x height); with stiles of " +
                                 num(sw) + " mm and rails of " + num(rw) + " mm the panel opening would be " + num(openW) +
                                 " x " + num(openH) + " mm, below the minimum of " + num(kMinPanelOpening) +
                                 " mm (generic default). Use narrower stiles / rails, a larger front or style Slab.");
    const bool tenon = f.joint == FrameJoint::StubTenon;
    const double tl = tenon ? d : 0.0, railLen = openW + 2 * tl;
    const double gw = f.panelThickness + g.v(param::kGrooveFit), mid = ft / 2;
    const auto R = EdgeUse::Raw, F = EdgeUse::Front;

    // Stiles: door orientation (length +Z, width +X, thickness +Y; face A inner). Left stile
    // inner edge L2 (y = sw), right stile inner edge L1 (y = 0).
    out.framed = true;
    out.left = g.addPart(pieces[1], name + " left stile", role, mr, H, sw, ft,
                         Transform::fromAxes(kUp, kRight, {x0, yOuter, z0}), Edges{F, R, F, F});
    out.right = g.addPart(pieces[2], name + " right stile", role, mr, H, sw, ft,
                          Transform::fromAxes(kUp, kRight, {x1 - sw, yOuter, z0}), Edges{R, F, F, F});
    // Rails: length +X, width -Z (L1 = upper edge), thickness +Y. Top rail inner edge L2,
    // bottom rail inner edge L1; the ends (tenons or dowelled butt ends) are hidden: raw.
    out.top = g.addPart(pieces[3], name + " top rail", role, mr, railLen, rw, ft,
                        Transform::fromAxes(kRight, kDown, {x0 + sw - tl, yOuter, z1}), Edges{F, R, R, R});
    out.bottom = g.addPart(pieces[4], name + " bottom rail", role, mr, railLen, rw, ft,
                           Transform::fromAxes(kRight, kDown, {x0 + sw - tl, yOuter, z0 + rw}), Edges{R, F, R, R});
    // Panel: opening + (groove depth - clearance) on every side, centred in the frame thickness.
    const double e = d - f.panelClearance, pw = openW + 2 * e, ph = openH + 2 * e;
    const double py = yInner - mid - f.panelThickness / 2;
    out.panel = horizontalGrain
                    ? g.addPart(pieces[5], name + " panel", role, mr, pw, ph, f.panelThickness,
                                Transform::fromAxes(kRight, kDown, {x0 + sw - e, py, z1 - rw + e}), Edges{})
                    : g.addPart(pieces[5], name + " panel", role, mr, ph, pw, f.panelThickness,
                                Transform::fromAxes(kUp, kRight, {x0 + sw - e, py, z0 + rw - e}), Edges{});
    panelMaterial(g, out.panel, f, pieces[5]);
    const char* kinds[] = {"stile", "stile", "rail", "rail", "panel"};
    const std::size_t idx[] = {out.left, out.right, out.top, out.bottom, out.panel};
    for (int k = 0; k < 5; ++k) {
        g.assembly.parts[idx[k]].custom[kPieceKey] = kinds[k];
        g.assembly.parts[idx[k]].custom[kFrontKey] = slot;
    }

    // Panel grooves (centred in the thickness). Stub tenons fill the stile groove at the rail
    // ends, so it runs through; with dowels it stops groove_depth beyond the panel opening.
    const double s0 = tenon ? 0.0 : rw - d, s1 = tenon ? H : H - rw + d;
    const char* note = "Groove for the frame panel";
    addGroove(g, out.left, domain::Face::L2, "groove.panel", s0, mid, s1, mid, gw, d, note);
    addGroove(g, out.right, domain::Face::L1, "groove.panel", s0, mid, s1, mid, gw, d, note);
    addGroove(g, out.top, domain::Face::L2, "groove.panel", 0, mid, railLen, mid, gw, d, note);
    addGroove(g, out.bottom, domain::Face::L1, "groove.panel", 0, mid, railLen, mid, gw, d, note);

    if (tenon) {
        // Stub tenon = groove width thick, groove depth long: a cheek cut (rebate across the
        // rail end, full rail width) on both faces of both ends.
        const double cheek = (ft - gw) / 2;
        for (const std::size_t r : {out.top, out.bottom})
            for (const auto& [end, u] : {std::pair{"left", tl / 2}, std::pair{"right", railLen - tl / 2}})
                for (const auto& [face, f2] : {std::pair{domain::Face::A, "a"}, std::pair{domain::Face::B, "b"}})
                    addGroove(g, r, face, std::string("tenon.") + end + "." + f2, u, 0, u, rw, tl, cheek,
                              "Stub tenon cheek");
        return out;
    }

    // Dowel joints: 2 dowels per joint in the solid rail width (rail width - groove depth),
    // at 1/4 and 3/4 of it from the rail's outer edge.
    const HardwareSelection& sel = g.def.hardware;
    const domain::HardwareDefinition* dw = g.libs.findHardware(sel.dowel);
    if (!dw)
        return core::makeError(core::ErrorCode::NotFound,
                               who + " is dowelled, but the dowel '" + sel.dowel.str() +
                                   "' is not in the hardware library.",
                               g.def.id.str());
    const auto dia = dw->params.find(hardware::keys::kDiameter), len = dw->params.find(hardware::keys::kLength);
    if (dia == dw->params.end() || len == dw->params.end())
        return core::makeError(core::ErrorCode::InvalidArgument,
                               who + ": the dowel '" + sel.dowel.str() + "' has no diameter / length parameters.",
                               g.def.id.str());
    const double zone = rw - d, depth = len->second / 2 + kDowelHoleExtra;
    if (zone < kDowelZoneDiameters * dia->second - core::kLengthEps)
        return constraint(g, who + ": the rails are " + num(rw) + " mm wide with a " + num(d) +
                                 " mm groove, leaving " + num(zone) + " mm for the dowels; " +
                                 num(kDowelZoneDiameters * dia->second) + " mm (" + num(kDowelZoneDiameters) +
                                 " x the dowel diameter of " + num(dia->second) +
                                 " mm, generic rule) are needed. Use wider rails or stub tenons.");
    const double p1 = zone / 4, p2 = 3 * zone / 4, yc = yInner - mid;
    const double bottomStile[2] = {p1, p2}, bottomRail[2] = {rw - p1, rw - p2};
    const double topStile[2] = {H - p1, H - p2}, topRail[2] = {p1, p2};
    struct J {
        const char* name;
        std::size_t stile;
        domain::Face sf;
        std::size_t rail;
        domain::Face rf;
        const double (&su)[2];
        const double (&ru)[2];
        geometry::Vec3 at;
    };
    const J joints[] = {
        {"bottom_left", out.left, domain::Face::L2, out.bottom, domain::Face::W1, bottomStile, bottomRail,
         {x0 + sw, yc, z0 + p1}},
        {"bottom_right", out.right, domain::Face::L1, out.bottom, domain::Face::W2, bottomStile, bottomRail,
         {x1 - sw, yc, z0 + p1}},
        {"top_left", out.left, domain::Face::L2, out.top, domain::Face::W1, topStile, topRail, {x0 + sw, yc, z1 - p1}},
        {"top_right", out.right, domain::Face::L1, out.top, domain::Face::W2, topStile, topRail, {x1 - sw, yc, z1 - p1}},
    };
    for (const J& j : joints)
        if (auto r = frameDowels(g, *dw, "hw.frame_dowels." + slot + "." + j.name, j.stile, j.sf, j.rail, j.rf, j.su,
                                 j.ru, dia->second, depth, j.at);
            !r)
            return core::makeError(r.error().code, who + ": " + r.error().message, g.def.id.str());
    return out;
}

namespace {
/// True when hole `h` of `p` and the machined box `gb` (part frame) share volume.
bool holeHitsBox(const domain::Part& p, const domain::Hole& h, const geometry::Aabb& gb) {
    const double L = p.length, W = p.width, T = p.thickness;
    int axis = 2;
    double a0 = 0, a1 = 0, c[3] = {0, 0, 0};
    const auto range = [&](double size, bool fromMax) {
        const double dep = h.through ? size : std::min(h.depth, size);
        a0 = fromMax ? size - dep : 0.0;
        a1 = fromMax ? size : dep;
    };
    switch (h.face) {
    case domain::Face::A: axis = 2, c[0] = h.u, c[1] = h.v, range(T, true); break;
    case domain::Face::B: axis = 2, c[0] = h.u, c[1] = h.v, range(T, false); break;
    case domain::Face::L1: axis = 1, c[0] = h.u, c[2] = h.v, range(W, false); break;
    case domain::Face::L2: axis = 1, c[0] = h.u, c[2] = h.v, range(W, true); break;
    case domain::Face::W1: axis = 0, c[1] = h.u, c[2] = h.v, range(L, false); break;
    case domain::Face::W2: axis = 0, c[1] = h.u, c[2] = h.v, range(L, true); break;
    }
    const double lo[3] = {gb.min.x, gb.min.y, gb.min.z}, hi[3] = {gb.max.x, gb.max.y, gb.max.z};
    const double eps = core::kLengthEps;
    if (std::min(a1, hi[axis]) - std::max(a0, lo[axis]) <= eps) return false;
    double dist2 = 0;
    for (int k = 0; k < 3; ++k) {
        if (k == axis) continue;
        const double q = std::clamp(c[k], lo[k], hi[k]) - c[k];
        dist2 += q * q;
    }
    const double r = h.diameter / 2 - eps;
    return r > 0 && dist2 < r * r;
}
} // namespace

void checkFrameHoles(Gen& g) {
    for (const domain::Part& p : g.assembly.parts) {
        if (!p.custom.count(kPieceKey)) continue;
        for (const auto& hop : p.operations) {
            const auto* h = std::get_if<domain::Hole>(&hop.shape);
            if (!h) continue;
            for (const auto& gop : p.operations) {
                const auto* gr = std::get_if<domain::Groove>(&gop.shape);
                if (!gr) continue;
                const geometry::Aabb gb = grooveBoxLocal(p, *gr);
                if (gb.empty() || !holeHitsBox(p, *h, gb)) continue;
                g.error("furniture.front.frame_hole_in_groove",
                        "Hole '" + hop.id.str() + "' on face " + domain::toString(h->face) + " at (" + num(h->u) + ", " +
                            num(h->v) + "), diameter " + num(h->diameter) + " mm, of '" + p.name + "' (" + p.id.str() +
                            ") breaks into '" + gop.id.str() + "' (" + gop.note +
                            "). Use wider stiles / rails or move the fitting.",
                        p.id.str());
                break;
            }
        }
    }
}

} // namespace fos::furniture::detail
