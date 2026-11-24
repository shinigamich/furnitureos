// SPDX-License-Identifier: AGPL-3.0-only
/// Fronts: doors (single / pair) and drawer-front stacks, with hinge and handle jobs.
#include "gen_context.hpp"

namespace fos::furniture::detail {

void handleJob(Gen& g, std::size_t front, const std::string& slot, const FrontSpec& spec, bool door, bool freeEdgeRight,
               bool onRail) {
    namespace p = param;
    if (spec.handle == HandlePosition::None) return;
    Job j;
    j.kind = JobKind::Handle;
    j.instance = "hw.handle." + slot;
    j.a = front;
    hardware::HandleConfig& c = j.handle;
    if (door) {
        c.orientation = hardware::HandleOrientation::Vertical;
        c.horizontalAnchor = freeEdgeRight ? hardware::HorizontalAnchor::FromRight : hardware::HorizontalAnchor::FromLeft;
        c.horizontalOffset = g.v(p::kHandleEdgeOffset);
    } else {
        c.orientation = hardware::HandleOrientation::Horizontal;
    }
    if (spec.handle == HandlePosition::Top) c.verticalAnchor = hardware::VerticalAnchor::FromTop;
    if (spec.handle == HandlePosition::Bottom) c.verticalAnchor = hardware::VerticalAnchor::FromBottom;
    c.verticalOffset = g.v(p::kHandleEndOffset);
    if (onRail) c.verticalAnchor = hardware::VerticalAnchor::Center;
    g.jobs.push_back(std::move(j));
}

namespace {

/// CRANK RULE: the hinge for a front follows its overlay kind: Full -> HardwareSelection::hinge
/// (0 mm crank), Half -> hingeHalf (9.5 mm crank), Inset -> hingeInset (16 mm crank). When the
/// half / inset hinge is not in the hardware library (e.g. a project library saved before
/// those entries existed), Half falls back to `hinge` with warning
/// furniture.front.half_overlay_hinge and Inset gets no hinges with warning
/// furniture.front.inset_hinges (std::nullopt).
std::optional<domain::HardwareId> hingeFor(Gen& g, OverlayKind overlay, const std::string& name,
                                           const std::string& slot) {
    const HardwareSelection& sel = g.def.hardware;
    const std::string who = "'" + name + "' (" + slot + ") of cabinet '" + g.def.id.str() + "'";
    if (overlay == OverlayKind::Full) return sel.hinge;
    if (overlay == OverlayKind::Half) {
        if (g.libs.findHardware(sel.hingeHalf)) return sel.hingeHalf;
        g.warn("furniture.front.half_overlay_hinge",
               "Front " + who + " is half overlay but the half-overlay hinge '" + sel.hingeHalf.str() +
                   "' is not in the hardware library; the full-overlay hinge '" + sel.hinge.str() + "' was used.",
               g.partId(slot));
        return sel.hinge;
    }
    if (g.libs.findHardware(sel.hingeInset)) return sel.hingeInset;
    g.warn("furniture.front.inset_hinges",
           "Front " + who + " is inset but the inset hinge '" + sel.hingeInset.str() +
               "' is not in the hardware library, so no hinges were placed.",
           g.partId(slot));
    return std::nullopt;
}

/// FRAME HANDLE RULE (frame-and-panel fronts; slab fronts: the front itself). A door's
/// vertical handle goes onto the stile at its free edge with the slab offsets. A horizontal
/// handle (drawer front, flap) goes onto a rail, centred in the rail width: Top -> top rail,
/// Bottom -> bottom rail, Center (the floating panel cannot carry a handle) -> top rail of a
/// drawer front, bottom rail of a flap.
void frontHandle(Gen& g, const FrontPieces& fp, const std::string& slot, const FrontSpec& spec, bool door,
                 bool freeEdgeRight, bool flap) {
    if (!fp.framed) return handleJob(g, fp.panel, slot, spec, door, freeEdgeRight);
    if (door) return handleJob(g, freeEdgeRight ? fp.right : fp.left, slot, spec, true, freeEdgeRight);
    const bool bottom = spec.handle == HandlePosition::Bottom || (spec.handle == HandlePosition::Center && flap);
    handleJob(g, bottom ? fp.bottom : fp.top, slot, spec, false, false, true);
}

} // namespace

core::Result<void> buildFront(Gen& g, const FrontSpec& spec, const std::string& path, const Box& box, const Bounds& b) {
    namespace p = param;
    const double ft = g.v(p::kFrontThickness), gap = g.v(p::kFrontGap);
    const bool inset = spec.overlay == OverlayKind::Inset;
    const std::string label = "Section '" + sectionLabel(path) + "' of cabinet '" + g.def.id.str() + "'";

    // How far the front reaches over (positive) or stays inside (negative) each bounding panel.
    const auto cover = [&](bool outer) {
        if (inset) return -g.v(p::kInsetGap);
        return spec.overlay == OverlayKind::Full && outer ? g.v(p::kOverlayOuter) : g.v(p::kOverlayShared);
    };
    const double x0 = box.x0 - cover(b.leftOuter), x1 = box.x1 + cover(b.rightOuter);
    const double z0 = box.z0 - cover(b.bottomOuter), z1 = box.z1 + cover(b.topOuter);
    if (x1 - x0 <= core::kLengthEps || z1 - z0 <= core::kLengthEps)
        return core::makeError(core::ErrorCode::ConstraintViolation,
                               label + ": the front would measure " + num(x1 - x0) + " x " + num(z1 - z0) +
                                   " mm; the opening is too small for the configured gaps.",
                               g.def.id.str());
    if (inset && g.v(p::kShelfSetback) < ft - core::kLengthEps) {
        // An inset front occupies y in [0, front_thickness] inside the carcass.
        const auto s = findSection(g.def.interior, path);
        if (s && s.value()->shelfCount > 0)
            return core::makeError(core::ErrorCode::ConstraintViolation,
                                   label + ": an inset front of " + num(ft) + " mm needs a shelf setback of at least " +
                                       num(ft) + " mm (is " + num(g.v(p::kShelfSetback)) + " mm), otherwise it collides "
                                       "with the shelves.",
                                   g.def.id.str());
    }
    // Front face plane: overlay fronts stand front_offset in front of the carcass (y < 0),
    // inset fronts are flush with the carcass front (y in [0, ft]).
    const double yInner = inset ? ft : -g.v(p::kFrontOffset); // y of the inner (rear) face

    if (spec.kind == FrontKind::DrawerStack) {
        const std::size_t n = spec.drawerHeights.size();
        const double available = z1 - z0 - static_cast<double>(n - 1) * gap;
        const std::string what = label + ", drawer stack (front height " + num(z1 - z0) + " mm, " + std::to_string(n - 1) +
                                 " gap(s) of " + num(gap) + " mm)";
        if (available <= core::kLengthEps)
            return core::makeError(core::ErrorCode::ConstraintViolation,
                                   what + ": the gaps alone need " + num(static_cast<double>(n - 1) * gap) + " mm.",
                                   g.def.id.str());
        auto heights = distribute(g, available, spec.drawerHeights, what);
        if (!heights) return core::makeError(heights.error().code, heights.error().message, g.def.id.str());
        double z = z0;
        for (std::size_t i = 0; i < n; ++i) {
            // Drawer front: length -> +X (grain horizontal), width -> -Z (from the top edge down),
            // thickness -> +Y. Like a door, face B (z = 0) is the visible outer face at
            // y = yInner - ft and face A (z = T) the inner, cabinet-side face at y = yInner.
            // L1 is the top edge, L2 the bottom edge.
            const std::string slot = joinSlot({"drawer", path, std::to_string(i + 1)});
            const double h = heights.value()[i];
            auto fp = addFrontPieces(g, spec, slot, "Drawer front " + std::to_string(i + 1),
                                     domain::PartRole::DrawerFront, true, x0, x1, z, z + h, yInner);
            if (!fp) return fp.error();
            frontHandle(g, fp.value(), slot, spec, false, false, false);
            if (const auto ds = drawerBoxFor(spec, i))
                if (auto r = buildDrawerBox(g, spec, *ds, slot, z, z + h, box, b); !r) return r;
            z += heights.value()[i] + gap;
        }
        return {};
    }

    // Doors: length -> +Z (grain vertical), width -> +X, thickness -> +Y (face A is the inner
    // face, face B the visible outer face). FRONT FACE RULE: for every front (door or drawer
    // front) face A is the inner, cabinet-side face.
    const auto addDoor = [&](const std::string& slot, const std::string& name, double xa, double xb,
                             bool hingeLeft) -> core::Result<void> {
        auto fp = addFrontPieces(g, spec, slot, name, domain::PartRole::Door, false, xa, xb, z0, z1, yInner);
        if (!fp) return fp.error();
        const std::size_t part = hingeLeft ? fp.value().left : fp.value().right; // hinge-side stile
        if (spec.hinges) {
            if (const auto hinge = hingeFor(g, spec.overlay, name, slot)) {
                Job j;
                j.kind = JobKind::Hinges;
                j.instance = "hw.hinges." + slot;
                j.a = part;
                j.b = hingeLeft ? b.left : b.right;
                j.sharedPanel = hingeLeft ? !b.leftOuter : !b.rightOuter;
                j.hinge = *hinge;
                g.jobs.push_back(std::move(j));
            }
        }
        frontHandle(g, fp.value(), slot, spec, true, hingeLeft, false);
        return {};
    };
    if (spec.kind == FrontKind::ApplianceFront) {
        addApplianceFront(g, spec, joinSlot({"appliance_front", path}), x0, x1, z0, z1, yInner);
        return {};
    }
    if (spec.kind == FrontKind::Flap) {
        // Lift-up flap: a door-shaped front hinged at its top edge to the panel above the
        // section (hinge axis X), held open by one lift mechanism on each side.
        const std::string slot = joinSlot({"flap", path});
        auto fp = addFrontPieces(g, spec, slot, "Flap", domain::PartRole::Door, false, x0, x1, z0, z1, yInner);
        if (!fp) return fp.error();
        const std::size_t part = fp.value().top; // hinges on the top rail (slab: the flap)
        if (spec.hinges) {
            if (b.top.size() != 1)
                return core::makeError(core::ErrorCode::ConstraintViolation,
                                       label + ": a flap is hinged to the panel above it, but the section is closed "
                                       "by " + std::to_string(b.top.size()) + " stretcher rails; use a full top panel "
                                       "or place the flap below a fixed shelf.",
                                       g.def.id.str());
            if (const auto hinge = hingeFor(g, spec.overlay, "Flap", slot)) {
                Job j;
                j.kind = JobKind::Hinges;
                j.instance = "hw.hinges." + slot;
                j.a = part;
                j.b = b.top[0];
                j.sharedPanel = !b.topOuter;
                j.hinge = *hinge;
                j.flap = true;
                g.jobs.push_back(std::move(j));
                for (const bool left : {true, false}) {
                    Job l;
                    l.kind = JobKind::Lift;
                    l.instance = "hw.lift." + slot + (left ? ".left" : ".right");
                    l.a = left ? fp.value().left : fp.value().right; // lift fixing on the stile
                    l.b = left ? b.left : b.right;
                    l.c = b.top[0];
                    l.sharedPanel = left ? !b.leftOuter : !b.rightOuter;
                    g.jobs.push_back(std::move(l));
                }
            }
        }
        frontHandle(g, fp.value(), slot, spec, false, false, true); // horizontal handle
        return {};
    }
    if (spec.kind == FrontKind::Door) {
        if (path.empty() && g.def.corner.kind == CornerKind::Blind) {
            // Blind corner: the front is split into the fixed blind panel and the door.
            auto door = buildBlindPanel(g, x0, x1, z0, z1, yInner);
            if (!door) return door.error();
            return addDoor("door", "Door", door.value().first, door.value().second, spec.hingeSide == HingeSide::Left);
        }
        return addDoor(joinSlot({"door", path}), "Door", x0, x1, spec.hingeSide == HingeSide::Left);
    }
    const double each = (x1 - x0 - gap) / 2;
    if (each <= core::kLengthEps)
        return core::makeError(core::ErrorCode::ConstraintViolation,
                               label + ": a pair of doors with a " + num(gap) + " mm gap does not fit into a front width of " +
                                   num(x1 - x0) + " mm.",
                               g.def.id.str());
    if (auto r = addDoor(joinSlot({"door", path, "left"}), "Left door", x0, x0 + each, true); !r) return r;
    return addDoor(joinSlot({"door", path, "right"}), "Right door", x1 - each, x1, false);
}

} // namespace fos::furniture::detail
