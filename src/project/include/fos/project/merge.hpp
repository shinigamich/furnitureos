// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Import (merge) furniture items from another project. The selected items of the SOURCE
/// project are appended to the TARGET project together with the library entries (materials,
/// edge bands, hardware) they reference. The library merge follows the component insertion
/// rules (components.hpp: planComponentInsert / insertComponent), applied once to the union of
/// the referenced entries so an entry shared by several items is merged only once.
/// All lengths are mm; placements map the item (assembly) frame into the project frame.
#include "fos/core/result.hpp"
#include "fos/formats/library.hpp"
#include "fos/project/commands.hpp"

#include <string>
#include <vector>

namespace fos::project {

/// What to merge and how.
struct MergeRequest {
    /// Source item ids to import, in any order (imported in SOURCE order). Empty = all items.
    std::vector<std::string> itemIds;
    /// Library entry whose id exists in the target with a different content (JSON encoding):
    /// Skip = keep the target's entry (the imported items then use it), Replace = overwrite the
    /// target's entry in place (affects target items using it too), RenameWithNewId = add the
    /// source entry under a new id "merge.libN" (first N not used by any library entry) and point
    /// the imported items at it. Identical entries are never duplicated.
    formats::ConflictPolicy conflictPolicy = formats::ConflictPolicy::Skip;
    /// Applied on top of each item's source placement: new placement = offset * source placement.
    /// Must be rigid.
    geometry::Transform offset = geometry::Transform::identity();
};

/// One imported item.
struct MergedItem {
    std::string sourceId; ///< Id in the source project.
    std::string newId;    ///< Id in the target (== sourceId unless it clashed).
    std::string name;
    bool renamed() const { return sourceId != newId; }
};

/// One library entry referenced by the imported items.
struct MergedLibraryEntry {
    formats::MergeEntry entry; ///< Category, action (Added/Skipped/Replaced/Renamed), incoming and result id.
    /// The target had an entry with this id and category whose content differs (the policy
    /// decided the action). False for Added entries and identical entries (Skipped).
    bool conflict = false;
};

/// Dry-run result of planMerge(): what mergeProjects() / commands::mergeItems() will do.
struct MergePlan {
    std::vector<MergedItem> items;            ///< Source order.
    std::vector<MergedLibraryEntry> library;  ///< Materials, then edge bands, then hardware; each by sorted incoming id.
    std::vector<const MergedLibraryEntry*> withAction(formats::MergeAction action) const;
    std::vector<const MergedLibraryEntry*> conflicts() const;
};

/// Default id of an imported item whose source id is already used in the target (or by an item
/// imported before it in the same merge): the trailing decimal digits of the id are stripped and
/// the first free "<prefix><n>" is used, n = 1, 2, ... ("cab1" with target cab1, cab2 -> "cab3";
/// "sink" -> "sink1" when "sink" is taken). `used` must contain every id in use.
std::string freeItemId(const std::string& sourceId, const std::vector<std::string>& used);

/// Plans the merge of `request` without changing anything. Errors: InvalidArgument (source has no
/// items, a requested id given twice, non-rigid offset), NotFound (a requested id is not a source
/// item; a referenced library entry is missing from the source libraries — both name the id),
/// formats::mergeLibrary errors.
core::Result<MergePlan> planMerge(const Project& target, const Project& source, const MergeRequest& request);

/// Result of mergeProjects().
struct MergeResult {
    Project project; ///< The merged target.
    MergePlan plan;
    Affected affected;
};

/// Applies the merge to a copy of `target`. Besides planMerge() errors it fails with
/// InvalidArgument when an imported item cannot be generated in the target (naming the item);
/// on error nothing is returned. Imported items are appended in source order; a panel's part id
/// becomes "<newId>/panel"; cabinet part ids derive from the item id; item references to renamed
/// library entries are rewritten. Stored drawing edits of the source are not imported.
core::Result<MergeResult> mergeProjects(const Project& target, const Project& source, const MergeRequest& request);

namespace commands {
/// Undoable form of mergeProjects() (execute with Document::execute); the plan is recomputed
/// against the document's project when executed. Affected: every item when a target entry was
/// replaced, otherwise the imported items; `libraries` when any entry was added/replaced/renamed.
Command mergeItems(Project source, MergeRequest request);
} // namespace commands

} // namespace fos::project
