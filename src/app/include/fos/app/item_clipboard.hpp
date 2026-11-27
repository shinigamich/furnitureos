// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Copy / cut / paste of furniture items through the system clipboard, within one project and
/// between projects. The clipboard text is JSON:
///   {"format": "furnitureos-items", "version": 1, "project": <project payload>}
/// where the payload is project::projectToJson() of a project holding ONLY the copied items
/// (project order) and the source project's libraries. Pasting is project::commands::mergeItems
/// (merge.hpp), so referenced materials / edge bands / hardware come along with the items under
/// the merge rules (identical entries reused, a differing entry with the same id added under a
/// new id: ConflictPolicy::RenameWithNewId) and clashing item ids are renamed (freeItemId()).
#include "fos/core/result.hpp"
#include "fos/geometry/vec.hpp"
#include "fos/project/project.hpp"

#include <string>
#include <vector>

namespace fos::app {

class Session;

/// MIME type the GUI stores the clipboard JSON under (plain text is set too).
inline constexpr const char* kItemsMimeType = "application/x-furnitureos-items+json";

/// Clipboard JSON of `itemIds` (any order; written in project order). Errors: empty list,
/// unknown id (NotFound).
core::Result<std::string> itemsToClipboard(const project::Project& project, const std::vector<std::string>& itemIds);

/// Parses clipboard text back into the source project. ParseError / InvalidArgument for text
/// that is not FurnitureOS item JSON (wrong format tag or version, invalid project payload, no
/// items).
core::Result<project::Project> itemsFromClipboard(const std::string& text);

/// Translation applied to pasted items: zero when the pasted items' generated boxes overlap no
/// generated item of the target (paste in place, e.g. after Cut or into another project),
/// otherwise +X so the pasted group starts at the right edge of all target items.
geometry::Vec3 pasteOffset(const project::Project& target, const project::Project& source);

/// Pastes clipboard text into the session (ONE undo step) and selects the pasted items.
/// Returns their ids in the target.
core::Result<std::vector<std::string>> pasteItems(Session& session, const std::string& text);

/// Copies the selected items (Selection::allItems()) to clipboard JSON.
core::Result<std::string> copySelectedItems(const Session& session);
/// Copies, then deletes the selected items (one undo step for the deletion).
core::Result<std::string> cutSelectedItems(Session& session);

} // namespace fos::app
