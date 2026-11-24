// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// View-model of the cabinet hardware selection and of per-part overrides (material and edge
/// bands of one generated part). Choices come from the project libraries; every change is
/// commands::setHardware / commands::setPartOverride (atomic in the furniture module).
#include "fos/project/commands.hpp"

#include <QString>

#include <optional>
#include <string>
#include <vector>

namespace fos::app {

/// The fittings of furniture::HardwareSelection, in editor order. SlidingSystem = track system
/// of sliding doors; Hanger = concealed wall-cabinet hanger (optional, empty = none); WallRail =
/// the rail the hangers hook onto (used by the engine only while a hanger is set).
enum class HardwareRole { Hinge, Handle, ShelfPin, Confirmat, Dowel, Foot, SlidingSystem, Hanger, WallRail };
inline constexpr HardwareRole kHardwareRoles[] = {
    HardwareRole::Hinge, HardwareRole::Handle,        HardwareRole::ShelfPin, HardwareRole::Confirmat, HardwareRole::Dowel,
    HardwareRole::Foot,  HardwareRole::SlidingSystem, HardwareRole::Hanger,   HardwareRole::WallRail};

/// Catalogue kind a role takes (Hinge, Handle, ShelfSupport, Confirmat, Dowel, Foot,
/// SlidingSystem, Suspension; WallRail: Other, restricted to entries with the rail keys
/// `rail_height` and `rail_thickness` of hardware/cabinet_hanger.hpp).
domain::HardwareKind hardwareKind(HardwareRole role);
/// True when the role may be empty ("None"): only Hanger.
bool hardwareRoleOptional(HardwareRole role);
/// Translated caption ("Hinge:", "Handle:", ...).
QString hardwareRoleTitle(HardwareRole role);
const domain::HardwareId& selected(const furniture::HardwareSelection& selection, HardwareRole role);

struct HardwareChoice {
    std::string id;
    std::string name;     ///< Library name (English data), or the id when missing.
    bool missing = false; ///< The current selection is not in the library (or of another kind).
    bool none = false;    ///< The "None" entry of an optional role (id and name empty).
};
/// Library entries of the role's kind in library order; the current id is appended as
/// `missing` when it is not among them, so the editor can still show it. An optional role
/// (hardwareRoleOptional) starts with the `none` entry.
std::vector<HardwareChoice> hardwareChoices(const furniture::Libraries& libraries, HardwareRole role,
                                            const std::string& current);
/// Translated hint for the hanger row: a hanger needs a Grooved back with groove_distance at
/// least the hanger's back space (hardware::hangerBackSpace of the selected hanger, else of
/// hardware::ids::kHangerGeneric when it is in the library; the number is left out when
/// neither is available) or no back; Overlay and Inset backs are refused by the engine.
QString hangerHint(const furniture::Libraries& libraries, const furniture::HardwareSelection& selection);
/// commands::setHardware with only `role` changed to `hardwareId`.
project::Command hardwareCommand(const furniture::CabinetDefinition& cabinet, HardwareRole role,
                                 const std::string& hardwareId);

/// Slot id of a generated part ("cab1/side.left" of item "cab1" -> "side.left"), or
/// std::nullopt when the part id does not belong to the item.
std::optional<std::string> partSlot(const std::string& itemId, const std::string& partId);

/// Override of one edge in the part editor.
struct EdgeOverrideChoice {
    enum class Kind { CabinetRule, Raw, Band };
    Kind kind = Kind::CabinetRule; ///< CabinetRule = no override entry for this edge.
    std::string bandId;            ///< Kind::Band only.
    bool operator==(const EdgeOverrideChoice&) const = default;
};
/// Current override of `slot` (empty PartOverride when there is none).
furniture::PartOverride currentOverride(const furniture::CabinetDefinition& cabinet, const std::string& slot);
EdgeOverrideChoice edgeChoice(const furniture::PartOverride& override, domain::EdgeSide side);
/// commands::setPartOverride with the material override set (std::nullopt = cabinet material)
/// and the edges (and contour) of the existing override kept. An override that ends up empty is removed.
project::Command partMaterialCommand(const furniture::CabinetDefinition& cabinet, const std::string& slot,
                                     const std::optional<std::string>& materialId);
/// As above for one edge.
project::Command partEdgeCommand(const furniture::CabinetDefinition& cabinet, const std::string& slot,
                                 domain::EdgeSide side, const EdgeOverrideChoice& choice);
/// Removes the material and edge overrides of `slot` (commands::setPartOverride); a contour
/// (Shape section) is kept.
project::Command clearPartOverrideCommand(const furniture::CabinetDefinition& cabinet,
                                          const std::string& slot);

} // namespace fos::app
