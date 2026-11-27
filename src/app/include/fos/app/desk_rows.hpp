// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Inspector view-model for desks and tables (furniture DeskSpec, furniture README "Desks and
/// tables"): the editable desk settings, the browser summary and the undoable command that
/// changes them. Width / height / depth / thickness are ordinary graph parameters (parameter
/// commands); everything else is judged by the furniture generator, whose refusal is returned.
#include "fos/project/commands.hpp"

#include <QString>

#include <string>

namespace fos::app {

/// True when the cabinet is a desk / table (CabinetDefinition::desk set).
bool isDesk(const furniture::CabinetDefinition& cabinet) noexcept;

/// What the inspector's Desk group shows / submits. All lengths mm.
struct DeskInput {
    furniture::DeskSupport support = furniture::DeskSupport::Panels;
    double frontCornerRadius = 0; ///< 0 = sharp front corners.
    bool modestyPanel = true;     ///< End panels only.
    double modestySetback = 100;
    double modestyHeight = 350;
    bool cableSlot = false;
    furniture::DeskCableCutout cable; ///< Used when cableSlot.
    bool pedestal = false;            ///< End panels only.
    furniture::DeskPedestal pedestalSpec; ///< Used when pedestal.
};

/// Editor state of a desk spec (absent optional parts keep the DeskSpec defaults in their
/// fields so that enabling them starts from the generic values).
DeskInput deskInput(const furniture::DeskSpec& spec);

/// The spec the editor submits: `base` (leg hardware id and leg inset are kept) with the input
/// applied. With Legs the modesty panel and the pedestal are dropped (the generator supports
/// them only between end panels); no other value is checked here.
furniture::DeskSpec deskSpec(const DeskInput& input, furniture::DeskSpec base = {});

/// Undoable command (furniture::applyEdit, atomic) replacing the desk settings of `itemId`
/// with deskSpec(input, current spec). Generic fittings the new spec needs that the library
/// lacks (the generic leg, furniture::requiredGenericHardware()) are added in the same step.
/// Fails without changes when the item is not a desk or the generator refuses the result
/// (its message). Label "Change desk of {0}".
project::Command deskCommand(const std::string& itemId, const DeskInput& input);

/// Translated one-line summary for the project browser, e.g. "Desk on end panels, modesty
/// panel, cable slot, 3-drawer pedestal right" or "Table on 4 legs, corner radius 50 mm".
/// Empty for anything but a desk.
QString deskDescription(const furniture::CabinetDefinition& cabinet);

} // namespace fos::app
