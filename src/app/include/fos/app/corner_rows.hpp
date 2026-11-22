// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Inspector view-model for corner cabinets (furniture CornerSpec, furniture README "Corner
/// cabinets"): which custom parameters belong to the corner construction, display texts and
/// the undoable command that moves the blind panel to the other side. Sizes are ordinary graph
/// parameters (furniture::corner_param) and are edited with the parameter commands
/// (parameter_rows.hpp); nothing is computed here.
#include "fos/app/parameter_rows.hpp"
#include "fos/project/commands.hpp"

#include <QString>

#include <string>
#include <vector>

namespace fos::app {

/// True for CornerKind::Blind and CornerKind::LShape.
bool isCornerCabinet(const furniture::CabinetDefinition& cabinet) noexcept;

/// Names of the corner parameters of `kind` in display order: Blind -> {blind_width};
/// LShape -> {corner_width_b, corner_depth_b}; None -> {}.
std::vector<std::string> cornerParameterNames(furniture::CornerKind kind);

/// One corner parameter of a cabinet: `row` is set when the graph has the parameter (a
/// missing one makes generation fail; the furniture message names it).
struct CornerParameter {
    std::string name;
    std::optional<ParameterRow> row;
};
/// cornerParameterNames(cabinet.corner.kind) with their graph rows (parameterGroups() data).
std::vector<CornerParameter> cornerParameters(const furniture::CabinetDefinition& cabinet);

/// `groups` without the rows named in `names` (groups that become empty are dropped).
std::vector<ParameterGroup> withoutParameters(std::vector<ParameterGroup> groups, const std::vector<std::string>& names);

/// Translated texts: "Blind corner" / "L-shaped corner" / "None"; "Left" / "Right".
QString cornerKindTitle(furniture::CornerKind kind);
QString cornerSideTitle(furniture::CornerSide side);
/// Translated caption of a corner parameter ("Blind panel width", "Leg B length",
/// "Leg B depth"); the parameter name for anything else.
QString cornerParameterTitle(const std::string& name);

/// One-line summary for the project browser, from the definition's graph values:
/// "Blind corner, blind panel left, 500 mm", "L-shaped corner, leg B 900 × 560 mm".
/// Empty for an ordinary cabinet. A missing parameter is shown as "?".
QString cornerDescription(const furniture::CabinetDefinition& cabinet);

/// Undoable command (furniture::applyEdit, atomic) that puts the blind panel of the blind
/// corner cabinet `itemId` on `side` and hinges its single root door on the opposite side
/// (the engine requires the door to open away from the blind panel). Fails without changes
/// when the item is not a blind corner cabinet or the result does not generate (message of
/// the furniture module). Label "Set blind side of {0} to {1}".
project::Command blindSideCommand(const std::string& itemId, furniture::CornerSide side);

} // namespace fos::app
