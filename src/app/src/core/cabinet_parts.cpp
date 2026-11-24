// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/app/cabinet_parts.hpp"

#include "fos/core/units.hpp"
#include "fos/hardware/cabinet_hanger.hpp"
#include "fos/hardware/catalog.hpp"

#include <QCoreApplication>

namespace fos::app {

namespace {
class HardwareText {
    Q_DECLARE_TR_FUNCTIONS(HardwareText)
};

template <class Selection> auto& field(Selection& s, HardwareRole role) {
    switch (role) {
    case HardwareRole::Hinge:
        return s.hinge;
    case HardwareRole::Handle:
        return s.handle;
    case HardwareRole::ShelfPin:
        return s.shelfPin;
    case HardwareRole::Confirmat:
        return s.confirmat;
    case HardwareRole::Dowel:
        return s.dowel;
    case HardwareRole::Foot:
        return s.foot;
    case HardwareRole::SlidingSystem:
        return s.slidingSystem;
    case HardwareRole::Hanger:
        return s.hanger;
    case HardwareRole::WallRail:
        return s.wallRail;
    }
    return s.hinge;
}

project::Command overrideCommand(const furniture::CabinetDefinition& cabinet, const std::string& slot,
                                 const furniture::PartOverride& o) {
    const bool empty = !o.material && o.edges.empty() && !o.contour;
    return project::commands::setPartOverride(
        cabinet.id.str(), slot, empty ? std::nullopt : std::optional<furniture::PartOverride>(o));
}
} // namespace

domain::HardwareKind hardwareKind(HardwareRole role) {
    switch (role) {
    case HardwareRole::Hinge:
        return domain::HardwareKind::Hinge;
    case HardwareRole::Handle:
        return domain::HardwareKind::Handle;
    case HardwareRole::ShelfPin:
        return domain::HardwareKind::ShelfSupport;
    case HardwareRole::Confirmat:
        return domain::HardwareKind::Confirmat;
    case HardwareRole::Dowel:
        return domain::HardwareKind::Dowel;
    case HardwareRole::Foot:
        return domain::HardwareKind::Foot;
    case HardwareRole::SlidingSystem:
        return domain::HardwareKind::SlidingSystem;
    case HardwareRole::Hanger:
        return domain::HardwareKind::Suspension;
    case HardwareRole::WallRail:
        return domain::HardwareKind::Other;
    }
    return domain::HardwareKind::Other;
}

QString hardwareRoleTitle(HardwareRole role) {
    switch (role) {
    case HardwareRole::Hinge:
        return HardwareText::tr("Hinge:");
    case HardwareRole::Handle:
        return HardwareText::tr("Handle:");
    case HardwareRole::ShelfPin:
        return HardwareText::tr("Shelf pin:");
    case HardwareRole::Confirmat:
        return HardwareText::tr("Confirmat screw:");
    case HardwareRole::Dowel:
        return HardwareText::tr("Dowel:");
    case HardwareRole::Foot:
        return HardwareText::tr("Foot:");
    case HardwareRole::SlidingSystem:
        return HardwareText::tr("Sliding system:");
    case HardwareRole::Hanger:
        return HardwareText::tr("Cabinet hanger:");
    case HardwareRole::WallRail:
        return HardwareText::tr("Wall rail:");
    }
    return {};
}

bool hardwareRoleOptional(HardwareRole role) {
    return role == HardwareRole::Hanger;
}

const domain::HardwareId& selected(const furniture::HardwareSelection& selection, HardwareRole role) {
    return field(selection, role);
}

std::vector<HardwareChoice> hardwareChoices(const furniture::Libraries& libraries, HardwareRole role,
                                            const std::string& current) {
    std::vector<HardwareChoice> out;
    bool found = false;
    if (hardwareRoleOptional(role)) {
        out.push_back({std::string(), std::string(), false, true});
        found = current.empty();
    }
    for (const auto& h : libraries.hardware) {
        if (h.kind != hardwareKind(role))
            continue;
        if (role == HardwareRole::WallRail && (!h.params.count("rail_height") || !h.params.count("rail_thickness")))
            continue;
        out.push_back({h.id.str(), h.name, false, false});
        found = found || h.id.str() == current;
    }
    if (!found && !current.empty())
        out.push_back({current, current, true, false});
    return out;
}

QString hangerHint(const furniture::Libraries& libraries, const furniture::HardwareSelection& selection) {
    const domain::HardwareDefinition* hanger = selection.hanger.empty() ? nullptr : libraries.findHardware(selection.hanger);
    if (!hanger)
        hanger = libraries.findHardware(domain::HardwareId(hardware::ids::kHangerGeneric));
    if (hanger) {
        if (auto space = hardware::hangerBackSpace(*hanger))
            return HardwareText::tr("A cabinet hanger needs a grooved back with a groove distance of at least %1 mm "
                                    "(the hanger's back space; parameter groove_distance) or no back. Overlay and "
                                    "inset backs are refused.")
                .arg(QString::fromStdString(core::formatLength(space.value())));
    }
    return HardwareText::tr("A cabinet hanger needs a grooved back with a groove distance of at least the hanger's "
                            "back space (parameter groove_distance) or no back. Overlay and inset backs are refused.");
}

project::Command hardwareCommand(const furniture::CabinetDefinition& cabinet, HardwareRole role,
                                 const std::string& hardwareId) {
    furniture::HardwareSelection s = cabinet.hardware;
    field(s, role) = domain::HardwareId(hardwareId);
    return project::commands::setHardware(cabinet.id.str(), s);
}

std::optional<std::string> partSlot(const std::string& itemId, const std::string& partId) {
    const std::string prefix = itemId + "/";
    if (itemId.empty() || partId.size() <= prefix.size() || partId.compare(0, prefix.size(), prefix) != 0)
        return std::nullopt;
    return partId.substr(prefix.size());
}

furniture::PartOverride currentOverride(const furniture::CabinetDefinition& cabinet,
                                        const std::string& slot) {
    const auto it = cabinet.materials.overrides.find(slot);
    return it == cabinet.materials.overrides.end() ? furniture::PartOverride{} : it->second;
}

EdgeOverrideChoice edgeChoice(const furniture::PartOverride& o, domain::EdgeSide side) {
    const auto it = o.edges.find(side);
    if (it == o.edges.end())
        return {};
    if (!it->second)
        return {EdgeOverrideChoice::Kind::Raw, {}};
    return {EdgeOverrideChoice::Kind::Band, it->second->str()};
}

project::Command partMaterialCommand(const furniture::CabinetDefinition& cabinet, const std::string& slot,
                                     const std::optional<std::string>& materialId) {
    furniture::PartOverride o = currentOverride(cabinet, slot);
    o.material =
        materialId ? std::optional<domain::MaterialId>(domain::MaterialId(*materialId)) : std::nullopt;
    return overrideCommand(cabinet, slot, o);
}

project::Command partEdgeCommand(const furniture::CabinetDefinition& cabinet, const std::string& slot,
                                 domain::EdgeSide side, const EdgeOverrideChoice& choice) {
    furniture::PartOverride o = currentOverride(cabinet, slot);
    switch (choice.kind) {
    case EdgeOverrideChoice::Kind::CabinetRule:
        o.edges.erase(side);
        break;
    case EdgeOverrideChoice::Kind::Raw:
        o.edges[side] = std::nullopt;
        break;
    case EdgeOverrideChoice::Kind::Band:
        o.edges[side] = domain::EdgeBandId(choice.bandId);
        break;
    }
    return overrideCommand(cabinet, slot, o);
}

project::Command clearPartOverrideCommand(const furniture::CabinetDefinition& cabinet,
                                          const std::string& slot) {
    // The shape is edited in its own section and survives "Remove override".
    furniture::PartOverride o;
    o.contour = currentOverride(cabinet, slot).contour;
    return overrideCommand(cabinet, slot, o);
}

} // namespace fos::app
