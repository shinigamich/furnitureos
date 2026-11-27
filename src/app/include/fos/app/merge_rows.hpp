// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// View-model of File > Import items from project… (project/merge.hpp): the source project's
/// items as a checklist, the conflict policies and a translated preview of what
/// project::planMerge() reports (renamed ids, library actions). The merge itself is the one
/// undoable command project::commands::mergeItems(); nothing is computed here.
#include "fos/core/result.hpp"
#include "fos/project/merge.hpp"

#include <QString>
#include <QStringList>

#include <filesystem>
#include <string>
#include <vector>

namespace fos::app {

/// Loads the `.fosp` to import from (project::loadProject: checksum, migrations, resolve).
core::Result<project::Project> loadMergeSource(const std::filesystem::path& path);

/// One item of the source project, source order.
struct MergeSourceRow {
    std::string id;
    QString name;
    QString kind; ///< Translated "Cabinet" / "Panel".
};
std::vector<MergeSourceRow> mergeSourceRows(const project::Project& source);

/// Conflict policies in combo order (Skip, Replace, RenameWithNewId) with translated texts.
struct ConflictPolicyChoice {
    formats::ConflictPolicy policy;
    QString text;
};
std::vector<ConflictPolicyChoice> conflictPolicyChoices();

/// MergeRequest of the checked ids (in any order) and the policy; offset identity (the items
/// keep their source placement). An empty `checked` would mean "all items" to planMerge, so the
/// dialog must not submit it (mergeAllowed()).
project::MergeRequest mergeRequest(const std::vector<std::string>& checked, formats::ConflictPolicy policy);
/// False when nothing is checked.
bool mergeAllowed(const std::vector<std::string>& checked) noexcept;

/// Translated preview, one line per imported item ("cab1 “Base cabinet” → cab3 (id already
/// used)") followed by one line per referenced library entry ("Material “Oak” (oak): added",
/// "… identical entry already in this project", "… differs: this project's entry is kept",
/// "… replaces this project's entry", "… differs: added as merge.lib1").
QStringList mergePreviewLines(const project::MergePlan& plan);

} // namespace fos::app
