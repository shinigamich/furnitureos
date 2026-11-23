// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Helpers shared by the view-models of the dialogs that build items over a straight run of
/// cabinets (Edit > Add worktop…, worktop_form.hpp; Edit > Add plinth…, plinth_form.hpp): the
/// cabinet checklist, free item ids, combo entries and the formatting of engine notes.
#include "fos/project/project.hpp"

#include <QString>
#include <QStringList>

#include <string>
#include <vector>

namespace fos::app {

struct Selection;

/// One cabinet item of the checklist (project order).
struct CabinetRunRow {
    std::string id;
    QString label;        ///< "Name (id)".
    bool checked = false; ///< Pre-selection, see cabinetRunRows().
};
/// Every cabinet item of the project. Checked: the selected item when it is a cabinet,
/// otherwise every cabinet (the dialog lets the user narrow the run).
std::vector<CabinetRunRow> cabinetRunRows(const project::Project& project, const Selection& selection);

/// One entry of a combo; `id` empty = the engine's automatic choice.
struct FormChoice {
    std::string id;
    QString text;
};
/// "Name — 18 mm (id)" for a library material.
QString materialChoiceText(const domain::Material& material);

/// Unused item id "<base><n>" (n = 1, 2, ...): no item has that id or an id starting with
/// "<base><n>." (the ids of the pieces of a split item).
std::string nextRunItemId(const project::Project& project, const std::string& base);

/// One line per diagnostic, "Warning: message" (translated severity, English engine message).
QStringList diagnosticLines(const core::Diagnostics& notes);

} // namespace fos::app
