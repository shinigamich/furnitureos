// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/furniture/definition.hpp"
#include "fos/furniture/desk_spec.hpp"

namespace fos::furniture {

const char* toString(DeskSupport s) noexcept { return s == DeskSupport::Legs ? "Legs" : "Panels"; }
const char* toString(DeskPedestalSide s) noexcept { return s == DeskPedestalSide::Left ? "Left" : "Right"; }

std::optional<DeskSupport> deskSupportFromString(std::string_view text) noexcept {
    if (text == "Panels") return DeskSupport::Panels;
    if (text == "Legs") return DeskSupport::Legs;
    return std::nullopt;
}

std::optional<DeskPedestalSide> deskPedestalSideFromString(std::string_view text) noexcept {
    if (text == "Left") return DeskPedestalSide::Left;
    if (text == "Right") return DeskPedestalSide::Right;
    return std::nullopt;
}

domain::HardwareDefinition genericLegDefinition() {
    domain::HardwareDefinition d;
    d.id = domain::HardwareId(kGenericLegId);
    d.kind = domain::HardwareKind::Leg;
    d.name = "Table leg with square mounting plate, cut to length (generic)";
    d.dimensionsVerified = false;
    d.params = {{leg_keys::kPlateSize, 80.0},         {leg_keys::kPlateHoleSpacing, 60.0},
                {leg_keys::kPlateHoleDiameter, 4.0}, {leg_keys::kPlateHoleDepth, 10.0},
                {leg_keys::kHeightMin, 400.0},       {leg_keys::kHeightMax, 1100.0}};
    return d;
}

std::vector<domain::HardwareDefinition> requiredGenericHardware(const CabinetDefinition& definition) {
    std::vector<domain::HardwareDefinition> out;
    if (definition.desk && definition.desk->support == DeskSupport::Legs && definition.desk->leg.str() == kGenericLegId)
        out.push_back(genericLegDefinition());
    return out;
}

} // namespace fos::furniture
