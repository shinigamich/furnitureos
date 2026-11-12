// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Ready-made cabinet definitions. A template is ONLY a pre-filled CabinetDefinition: every
/// parameter, option, material and section can be edited afterwards (edit.hpp).
/// Sizes are W x H x D in mm; H includes the plinth. See README.md for the full defaults.
#include "fos/furniture/definition.hpp"

namespace fos::furniture::templates {

/// Materials every template starts with.
struct TemplateMaterials {
    domain::MaterialId carcass;
    domain::MaterialId back;
    domain::MaterialId front; ///< Empty = carcass material.
    domain::EdgeBandId visibleEdge;
    std::optional<domain::EdgeBandId> hiddenEdge;
};

/// 600 x 820 x 560; plinth panel 100 (sides to the floor); stretcher rails; overlay back;
/// 1 shelf; pair of full-overlay doors, handles near the top.
CabinetDefinition baseCabinet(domain::AssemblyId id, const TemplateMaterials& materials);
/// 600 x 720 x 320; full top; overlay back; 2 shelves; one door hinged left, handle near the bottom.
CabinetDefinition wallCabinet(domain::AssemblyId id, const TemplateMaterials& materials);
/// 600 x 2100 x 560; plinth panel 100; fixed shelf above a 700 mm lower section (1 shelf),
/// upper section with 3 shelves; one full-height door hinged left.
CabinetDefinition tallCabinet(domain::AssemblyId id, const TemplateMaterials& materials);
/// 600 x 820 x 560; feet 100; full top; grooved back; stack of 4 drawer FRONTS (bottom two
/// double height). No drawer boxes are generated (see README limitations).
CabinetDefinition drawerUnit(domain::AssemblyId id, const TemplateMaterials& materials);
/// 1000 x 2200 x 580; plinth panel 80; one partition (two equal sections: 4 shelves left,
/// 1 shelf right); pair of doors over the whole front.
CabinetDefinition wardrobeSection(domain::AssemblyId id, const TemplateMaterials& materials);
/// 800 x 1800 x 300; no plinth, no fronts; one fixed shelf in the middle, 2 adjustable
/// shelves in each half; overlay back.
CabinetDefinition openShelving(domain::AssemblyId id, const TemplateMaterials& materials);

} // namespace fos::furniture::templates
