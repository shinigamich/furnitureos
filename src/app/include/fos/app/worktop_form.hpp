// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// View-model of Edit > Add worktop… (project/worktop.hpp, project README section 4c): the
/// cabinet checklist, the material / front band choices, the live preview text from
/// project::planWorktop and the undoable apply (project::commands::addWorktop, one undo step).
/// All geometry, joints and cut-out rules are the project module's; nothing is computed here
/// beyond formatting. Lengths are mm.
#include "fos/project/worktop.hpp"

#include <QString>
#include <QStringList>

#include <string>
#include <vector>

namespace fos::app {

class Session;
struct Selection;

/// One cabinet item of the checklist (project order).
struct WorktopCabinetRow {
    std::string id;
    QString label;        ///< "Name (id)".
    bool checked = false; ///< Pre-selection, see worktopCabinetRows().
};
/// Every cabinet item of the project. Checked: the selected item when it is a cabinet,
/// otherwise every cabinet (the dialog lets the user narrow the run).
std::vector<WorktopCabinetRow> worktopCabinetRows(const project::Project& project, const Selection& selection);

/// One entry of the material or band combo; `id` empty = the engine's automatic choice.
struct WorktopChoice {
    std::string id;
    QString text;
};
/// "Generic placeholder (38 mm)" (id empty, the engine adds kGenericWorktopMaterial unless
/// it is already in the library) followed by the library materials in library order,
/// "Name — 38 mm (id)". The generic material itself is listed once, as the first entry.
std::vector<WorktopChoice> worktopMaterialChoices(const project::Project& project);
/// "Automatic" (id empty: first library band covering the thickness, else a generic
/// placeholder) followed by the library bands, "Name — 2 × 45 mm (id)".
std::vector<WorktopChoice> worktopBandChoices(const project::Project& project);

/// Translated "Sink" / "Hob", and both kinds in display order.
QString worktopCutoutKindTitle(project::WorktopCutoutKind kind);
const std::vector<project::WorktopCutoutKind>& worktopCutoutKinds();

/// Unused worktop item id "worktop<n>" (n = 1, 2, ...): no item has that id or an id
/// starting with "worktop<n>." (the ids of the pieces of a split top).
std::string nextWorktopId(const project::Project& project);

/// Engine defaults (WorktopRequest{}) with a free id, a unique name ("Worktop", "Worktop 2",
/// ...) and `itemIds` (first = reference cabinet).
project::WorktopRequest defaultWorktopRequest(const project::Project& project, std::vector<std::string> itemIds);

/// Result of planning the current form values.
struct WorktopPreview {
    bool ok = false;
    QString text;  ///< Multi-line summary when ok; empty otherwise.
    QString error; ///< The engine's refusal message (English, from the core) when not ok.
    std::vector<std::string> pieceIds; ///< Item ids the command will add, left to right.
};
/// Runs project::planWorktop and formats total length / depth / thickness, the pieces with
/// their lengths, the joint positions (from the left end) and the engine notes with their
/// severity.
WorktopPreview worktopPreview(const project::Project& project, const project::WorktopRequest& request);

/// Executes project::commands::addWorktop (one undo step) and selects the first new piece.
/// Returns the new item ids, left to right; on error the project is unchanged.
core::Result<std::vector<std::string>> applyWorktop(Session& session, const project::WorktopRequest& request);

} // namespace fos::app
