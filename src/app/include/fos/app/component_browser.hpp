// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Widget-free model of the "Components" dock: library folders, the category tree filtered
/// by tag and search text, the details of one component, inserting a component into the
/// session's project (project::insertComponent, undoable) and saving an item as a component
/// file (project::makeComponent + project::saveComponents). All lengths are mm.
#include "fos/app/session.hpp"
#include "fos/project/components.hpp"

#include <QString>
#include <QStringList>

#include <filesystem>
#include <string>
#include <vector>

namespace fos::app {

/// File extension of component files, including the dot.
inline constexpr const char* kComponentFileExtension = ".foscomp";

/// The per-user component folder: QStandardPaths::AppDataLocation + "/components". It is
/// created when missing (`create`); an empty path when no writable location exists.
std::filesystem::path defaultComponentFolder(bool create = true);

/// One category node of the browser tree with the entries that pass the filter.
struct ComponentCategoryRow {
    std::string category;                              ///< "" = uncategorised.
    std::vector<const project::ComponentEntry*> entries; ///< In ComponentLibrary::entries() order.
};

/// What the details pane shows.
struct ComponentDetails {
    QString name, description, author, category, source;
    QStringList tags;
    /// "600 × 820 × 560 mm" (width × height × depth); "unknown" parts as "?" (preview size 0).
    QString size;
    /// "Name (id)" of every embedded material / edge band / hardware entry, library order.
    QStringList materials, edgeBands, hardware;
};

/// Index of the components of the configured folders, filtered for display.
class ComponentBrowserModel {
public:
    /// Clears the index and scans `folders` (ComponentLibrary::scan). Returns the scan
    /// diagnostics (unreadable files, missing folders, duplicate ids).
    core::Diagnostics rescan(const std::vector<std::filesystem::path>& folders);
    const project::ComponentLibrary& library() const noexcept { return library_; }
    const std::vector<std::filesystem::path>& folders() const noexcept { return folders_; }

    /// Categories (sorted, uncategorised last) with the entries that carry `tag` (empty =
    /// any) and whose name, id, description, category or a tag contains `search`
    /// (case-insensitive; empty = all). Categories without matching entries are omitted.
    std::vector<ComponentCategoryRow> rows(const std::string& tag, const QString& search) const;

private:
    project::ComponentLibrary library_;
    std::vector<std::filesystem::path> folders_;
};

ComponentDetails componentDetails(const project::ComponentEntry& entry);

/// Ids of the embedded library entries that conflict with different entries of the same id
/// in `project` (planComponentInsert with RenameWithNewId: the entries it would rename),
/// formatted "material: <id>", "edge band: <id>", "hardware: <id>". Identical entries are
/// not conflicts. Empty = insert without asking.
core::Result<QStringList> componentConflicts(const project::Project& project, const project::Component& component);

/// Inserts `component` into the session's project as one undoable command: new id
/// nextItemId(project, "cab" or "panel"), placed at x = Session::rightEdge() (to the right
/// of the existing items, like templates), library conflicts resolved with `policy`
/// (RenameWithNewId uses componentIdGenerator). Selects the new item; returns its id.
core::Result<std::string> insertComponentIntoSession(Session& session, const project::Component& component,
                                                     formats::ConflictPolicy policy);

/// File name for a component id: the id with characters other than ASCII letters, digits,
/// '.', '-' and '_' replaced by '-', plus kComponentFileExtension ("comp.base-600.foscomp").
std::string componentFileName(const std::string& componentId);

/// makeComponent(project, itemId, meta) written to `folder`/componentFileName(id). The
/// folder is created when missing. AlreadyExists (naming the file) when the file exists and
/// `overwrite` is false. Returns the written path.
core::Result<std::filesystem::path> saveItemAsComponent(const project::Project& project, const std::string& itemId,
                                                        formats::ComponentMeta meta,
                                                        const std::filesystem::path& folder, bool overwrite = false);

/// "kitchen, base ,, kitchen" -> {"kitchen", "base"}: trimmed, non-empty, first occurrence kept.
std::vector<std::string> parseTagList(const QString& text);

} // namespace fos::app
