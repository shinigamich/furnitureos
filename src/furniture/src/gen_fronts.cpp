// SPDX-License-Identifier: AGPL-3.0-only
/// Fronts: doors (single / pair) and drawer-front stacks, with hinge and handle jobs.
#include "gen_context.hpp"

namespace fos::furniture::detail {
namespace {

void handleJob(Gen& g, std::size_t front, const std::string& slot, const FrontSpec& spec, bool door, bool freeEdgeRight) {
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
    g.jobs.push_back(std::move(j));
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
    const Edges all{EdgeUse::Front, EdgeUse::Front, EdgeUse::Front, EdgeUse::Front};
    const auto role = MaterialRole::Front;

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
            // Drawer front: length -> +X, width -> +Z, thickness -> -Y (face A is the inner face).
            const std::string slot = joinSlot({"drawer", path, std::to_string(i + 1)});
            const std::size_t part =
                g.addPart(slot, "Drawer front " + std::to_string(i + 1), domain::PartRole::DrawerFront, role, x1 - x0,
                          heights.value()[i], ft, Transform::fromAxes(kRight, kUp, {x0, yInner, z}), all);
            handleJob(g, part, slot, spec, false, false);
            z += heights.value()[i] + gap;
        }
        return {};
    }

    // Doors: length -> +Z (grain vertical), width -> +X, thickness -> +Y (face A is the inner face).
    const auto addDoor = [&](const std::string& slot, const std::string& name, double xa, double xb, bool hingeLeft) {
        const std::size_t part = g.addPart(slot, name, domain::PartRole::Door, role, z1 - z0, xb - xa, ft,
                                           Transform::fromAxes(kUp, kRight, {xa, yInner - ft, z0}), all);
        if (spec.hinges) {
            if (inset) {
                g.warn("furniture.front.inset_hinges",
                       "Door '" + name + "' (" + slot + ") of cabinet '" + g.def.id.str() +
                           "' is inset; no hinge rule for inset doors is available, so no hinges were placed.",
                       g.partId(slot));
            } else {
                Job j;
                j.kind = JobKind::Hinges;
                j.instance = "hw.hinges." + slot;
                j.a = part;
                j.b = hingeLeft ? b.left : b.right;
                j.sharedPanel = hingeLeft ? !b.leftOuter : !b.rightOuter;
                g.jobs.push_back(std::move(j));
            }
        }
        handleJob(g, part, slot, spec, true, hingeLeft);
    };
    if (spec.kind == FrontKind::Door) {
        addDoor(joinSlot({"door", path}), "Door", x0, x1, spec.hingeSide == HingeSide::Left);
        return {};
    }
    const double each = (x1 - x0 - gap) / 2;
    if (each <= core::kLengthEps)
        return core::makeError(core::ErrorCode::ConstraintViolation,
                               label + ": a pair of doors with a " + num(gap) + " mm gap does not fit into a front width of " +
                                   num(x1 - x0) + " mm.",
                               g.def.id.str());
    addDoor(joinSlot({"door", path, "left"}), "Left door", x0, x0 + each, true);
    addDoor(joinSlot({"door", path, "right"}), "Right door", x1 - each, x1, false);
    return {};
}

} // namespace fos::furniture::detail
