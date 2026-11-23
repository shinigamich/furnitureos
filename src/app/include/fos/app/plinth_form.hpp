// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// View-model of Edit > Add plinth… (project/plinth.hpp, project README section 4d): the
/// cabinet checklist (shared with the worktop dialog, cabinet_run_form.hpp), the material
/// choice, the live preview text from project::planPlinth and the undoable apply
/// (project::commands::addPlinth, one undo step). All geometry and joint rules are the project
/// module's; nothing is computed here beyond formatting. Lengths are mm.
#include "fos/app/cabinet_run_form.hpp"
#include "fos/project/plinth.hpp"

#include <QString>

#include <string>
#include <vector>

namespace fos::app {

class Session;

/// "Carcass material of the first cabinet" (id empty: the engine's default) followed by the
/// library materials in library order, "Name — 18 mm (id)".
std::vector<FormChoice> plinthMaterialChoices(const project::Project& project);

/// Unused plinth item id "plinth<n>" (n = 1, 2, ...), also free as a prefix "plinth<n>." (the
/// ids of the pieces and returns).
std::string nextPlinthId(const project::Project& project);

/// Engine defaults (PlinthRequest{}: setback 50 mm, height from the cabinets, no returns,
/// stock length) with a free id, a unique name ("Plinth", "Plinth 2", ...) and `itemIds`
/// (first = reference cabinet).
project::PlinthRequest defaultPlinthRequest(const project::Project& project, std::vector<std::string> itemIds);

/// Result of planning the current form values.
struct PlinthPreview {
    bool ok = false;
    QString text;  ///< Multi-line summary when ok; empty otherwise.
    QString error; ///< The engine's refusal message (English, from the core) when not ok.
    std::vector<std::string> itemIds; ///< Item ids the command will add (front pieces left to right, then returns).
};
/// Runs project::planPlinth and formats length / height / thickness / setback position, the
/// front pieces with their extents, the butt joints (from the left end), the returns and the
/// engine notes with their severity.
PlinthPreview plinthPreview(const project::Project& project, const project::PlinthRequest& request);

/// Executes project::commands::addPlinth (one undo step) and selects the first new item.
/// Returns the new item ids; on error the project is unchanged.
core::Result<std::vector<std::string>> applyPlinth(Session& session, const project::PlinthRequest& request);

} // namespace fos::app
