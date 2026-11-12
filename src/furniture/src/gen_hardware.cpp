// SPDX-License-Identifier: AGPL-3.0-only
/// Runs the deferred hardware placements through the rules of fos::hardware.
#include "fos/hardware/corner_joint.hpp"
#include "fos/hardware/hinges.hpp"
#include "fos/hardware/shelf_supports.hpp"
#include "gen_context.hpp"

#include <algorithm>

namespace fos::furniture::detail {
namespace {

/// A hole drilled into a panel that may be drilled at the same spot from its other face
/// (interior partition) must stop before the mid-plane: at most (T - 2) / 2 deep.
domain::HardwareDefinition limitDepth(domain::HardwareDefinition def, const char* key, double panelThickness) {
    const auto it = def.params.find(key);
    if (it != def.params.end()) it->second = std::min(it->second, (panelThickness - 2.0) / 2.0);
    return def;
}

core::Result<hardware::Placement> place(Gen& g, const Job& j, const domain::HardwareInstanceId& id) {
    namespace p = param;
    const auto& parts = g.assembly.parts;
    const HardwareSelection& sel = g.def.hardware;
    const auto lookup = [&](const domain::HardwareId& hid, const char* what) -> core::Result<domain::HardwareDefinition> {
        if (const auto* d = g.libs.findHardware(hid)) return *d;
        return core::makeError(core::ErrorCode::NotFound,
                               std::string("The ") + what + " '" + hid.str() + "' selected for cabinet '" + g.def.id.str() +
                                   "' is not in the hardware library.");
    };
    switch (j.kind) {
    case JobKind::Corner: {
        auto def = lookup(j.fastener == JointFastener::Confirmat ? sel.confirmat : sel.dowel, "joint fastener");
        if (!def) return def.error();
        auto joint = hardware::detectButtJoint(parts[j.a], parts[j.b]);
        if (!joint) return joint.error();
        hardware::CornerJointConfig c;
        c.instanceId = id;
        c.minEndOffset = std::min(g.v(p::kJointEndOffset), joint.value().along.length() / 4);
        c.maxSpacing = g.v(p::kJointMaxSpacing);
        return hardware::connectCorner(parts[j.a], parts[j.b], def.value(), c);
    }
    case JobKind::ShelfPins: {
        auto def = lookup(sel.shelfPin, "shelf support");
        if (!def) return def.error();
        hardware::ShelfSupportConfig c;
        c.instanceId = id;
        c.setbackFront = g.v(p::kShelfPinFront);
        c.setbackBack = g.v(p::kShelfPinBack);
        c.maxGap = std::max(2.0, g.v(p::kShelfClearance) + 0.5);
        const auto pin = j.sharedPanel ? limitDepth(def.value(), hardware::keys::kHoleDepth, g.v(p::kThickness)) : def.value();
        return hardware::placeShelfSupports(parts[j.a], parts[j.b], parts[j.c], pin, c);
    }
    case JobKind::Hinges: {
        auto def = lookup(sel.hinge, "hinge");
        if (!def) return def.error();
        hardware::HingeConfig c;
        c.instanceId = id;
        c.offsetFromTop = c.offsetFromBottom = g.v(p::kHingeOffset);
        c.maxFrontGap = std::max(c.maxFrontGap, g.v(p::kFrontOffset));
        const auto hinge =
            j.sharedPanel ? limitDepth(def.value(), hardware::keys::kPlateHoleDepth, g.v(p::kThickness)) : def.value();
        return hardware::placeHinges(parts[j.a], parts[j.b], hinge, c);
    }
    case JobKind::Handle: {
        auto def = lookup(sel.handle, "handle");
        if (!def) return def.error();
        hardware::HandleConfig c = j.handle;
        c.instanceId = id;
        return hardware::placeHandle(parts[j.a], def.value(), c);
    }
    }
    return core::makeError(core::ErrorCode::Internal, "Unknown hardware job.");
}

} // namespace

void runHardware(Gen& g) {
    for (const Job& j : g.jobs) {
        const domain::HardwareInstanceId id(g.partId(j.instance));
        auto placement = place(g, j, id);
        if (placement) {
            if (auto stored = hardware::applyPlacement(g.assembly, placement.value()); !stored)
                placement = stored.error();
        }
        if (!placement)
            g.error(placement.error().code == core::ErrorCode::NotFound ? "furniture.hardware.missing_definition"
                                                                        : "furniture.hardware.placement_failed",
                    "Hardware '" + id.str() + "' was not placed: " + placement.error().message, id.str());
    }
}

} // namespace fos::furniture::detail
