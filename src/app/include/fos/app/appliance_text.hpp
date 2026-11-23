// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Display texts for appliance housings (furniture README "Appliance housings"): the project
/// browser summary of an appliance gap cabinet and the vent choices of an appliance niche.
/// Nothing is computed here: the gap width is the graph value of the custom parameter
/// furniture::appliance_param::kGapWidth. All appliance sizes are GENERIC; the appliance's
/// installation sheet is authoritative.
#include "fos/furniture/definition.hpp"

#include <QString>

namespace fos::app {

/// True when the cabinet is an appliance gap (CabinetDefinition::applianceGap set).
bool isApplianceGap(const furniture::CabinetDefinition& cabinet) noexcept;

/// One-line summary for the project browser, e.g. "Appliance gap 600 mm, end panel right"
/// ("end panels left and right", "no end panel"). The gap width is read from the graph
/// (`appliance_gap_width`); "?" when the parameter is missing. Empty when the cabinet is not
/// an appliance gap. Translated.
QString applianceGapDescription(const furniture::CabinetDefinition& cabinet);

/// Translated name of a niche vent: "No ventilation opening", "Slot in the shelf below",
/// "Cut-out in the back panel".
QString nicheVentTitle(furniture::NicheVent vent);

} // namespace fos::app
