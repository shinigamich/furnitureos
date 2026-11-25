// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Reusable COMPONENT LIBRARY (roadmap phase 14 groundwork). A component is one furniture item
/// (a parametric cabinet or a free panel) together with copies of exactly the library entries
/// (materials, edge bands, hardware definitions) it references, so it is self-contained and can
/// be inserted into any project. Components are stored in `.foscomp` files (container kind
/// formats::kComponentFormatKind; schema in src/project/README.md "Component files").
/// All lengths are mm; placements map the item (assembly) frame into the project frame.
#include "fos/core/diagnostic.hpp"
#include "fos/formats/component.hpp"
#include "fos/project/commands.hpp"

#include <map>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace fos::project {

/// A decoded component.
struct Component {
    formats::ComponentMeta meta;
    /// The item with identity placement. Its ids (assembly id, panel part id) are replaced on
    /// insertion; the stored ones are only the ids it had when it was captured.
    Item item;
    /// Exactly the entries libraryReferences(item) names, nothing else.
    formats::Library library;
    formats::Json extra = formats::Json::object(); ///< Unknown keys of the component object (preserved).
};

/// Library ids an item references, each list sorted and without duplicates. Empty ids
/// (e.g. "front = same as carcass") are not listed. Cabinets: every MaterialAssignment id
/// (incl. drawer / drawer bottom), edge bands of the assignment and of the part overrides,
/// override materials, every HardwareSelection id and the slide of every drawer box in the
/// interior tree. Panels: material and edge bands.
struct LibraryRefs {
    std::vector<std::string> materials;
    std::vector<std::string> edgeBands;
    std::vector<std::string> hardware;
};
LibraryRefs libraryReferences(const Item& item);

/// incomingId -> resultId of renamed library entries, per category.
struct LibraryRemap {
    std::map<std::string, std::string> materials, edgeBands, hardware;
    bool empty() const { return materials.empty() && edgeBands.empty() && hardware.empty(); }
};
/// Rewrites every library reference of `item` (the same fields as libraryReferences) through `remap`.
void remapLibraryReferences(Item& item, const LibraryRemap& remap);

/// Captures item `itemId` of `project` as a component. The item is copied with identity
/// placement; the referenced library entries are copied from project.libraries.
/// `meta` defaults: empty name -> the item name; empty id -> "comp." + a slug of the name
/// (lower-case ASCII letters and digits, other runs replaced by '-'); a preview size of all
/// zeros -> the size of the generated assembly's bounding box (x = width, z = height,
/// y = depth). Errors: NotFound (no such item, or a referenced entry missing from the
/// project libraries, naming the id), InvalidArgument (the item cannot be generated).
core::Result<Component> makeComponent(const Project& project, const std::string& itemId, formats::ComponentMeta meta);

/// File record of a component (definition encoded with toJson(const Item&)).
formats::ComponentRecord toRecord(const Component& component);
/// Decodes a record (itemFromJson at "<path>/definition") and checks that every referenced
/// id is embedded (ParseError "<path>/library: ... references material \"x\" which is not
/// embedded ..."). `path` prefixes messages, e.g. "/payload/components/0".
core::Result<Component> componentFromRecord(const formats::ComponentRecord& record, std::string_view path);

/// Writes `components` (at least one; unique ids and names) as one `.foscomp` file
/// (atomic, formats::saveDocument; backups per options). Nothing is written on error.
core::Result<formats::SaveReport> saveComponents(const std::filesystem::path& path,
                                                 const std::vector<Component>& components,
                                                 const formats::SaveOptions& options = {});
/// Reads and fully decodes a `.foscomp` file. Every error message names the file path and
/// a JSON pointer into it.
core::Result<std::vector<Component>> loadComponents(const std::filesystem::path& path,
                                                    const formats::LoadOptions& options = {},
                                                    core::Diagnostics* warnings = nullptr);
/// Same for text already in memory (`sourceName` names it in messages).
core::Result<std::vector<Component>> parseComponents(std::string_view text, std::string_view sourceName,
                                                     const formats::LoadOptions& options = {},
                                                     core::Diagnostics* warnings = nullptr);

/// Component whose id equals `idOrName`, else the first whose name equals it, else null.
const Component* findComponent(const std::vector<Component>& components, std::string_view idOrName);

/// Default id generator of insertComponent() for ConflictPolicy::RenameWithNewId:
/// "<newItemId>.lib1", "<newItemId>.lib2", ... (deterministic; mergeLibrary skips ids in use).
formats::IdGenerator componentIdGenerator(const std::string& newItemId);

/// Dry run of insertComponent(): how the embedded library entries would merge into
/// project.libraries (same rules as insertComponent()). The project is not changed. Pass
/// componentIdGenerator(newItemId) to get the ids insertComponent() would use (an empty
/// generator means mergeLibrary's random UUIDs).
core::Result<formats::MergeReport> planComponentInsert(const Project& project, const Component& component,
                                                       formats::ConflictPolicy policy,
                                                       const formats::IdGenerator& newId = {});

/// Undoable command (execute with Document::execute) that
///  1. merges component.library into the project libraries with `policy`
///     (Skip = keep the existing entry with the same id, Replace = overwrite it in place,
///     RenameWithNewId = add the incoming entry under a new id from `newId`, default
///     componentIdGenerator(newItemId)). An incoming entry IDENTICAL (same JSON encoding) to
///     the existing entry with the same id is always kept as is (reported Skipped), so e.g.
///     the built-in hardware is never duplicated by RenameWithNewId;
///  2. rewrites the item's references of renamed entries to their new ids;
///  3. appends the item with id `newItemId` (a panel's part id becomes "<newItemId>/panel"),
///     name component.meta.name and `placement`; cabinet part ids derive from the item id;
///  4. checks that the new item generates (generateItem); otherwise nothing changes.
/// Errors: InvalidArgument (empty id, non-rigid placement, generation failure),
/// AlreadyExists (item id in use), and mergeLibrary errors. Affected: every item when an
/// existing entry was replaced (other items may use it), otherwise only the new item;
/// `libraries` when any entry was added, replaced or renamed.
Command insertComponent(Component component, std::string newItemId, geometry::Transform placement,
                        formats::ConflictPolicy policy, formats::IdGenerator newId = {});

/// One indexed component.
struct ComponentEntry {
    std::string source;   ///< File path (generic form) or the caller's label (add()).
    Component component;
};

/// Index of components found in files and directories, for browsing by category and tag.
/// Order is deterministic: entries() is sorted by (category, name, id, source), byte-wise.
class ComponentLibrary {
public:
    /// Adds every component of every `.foscomp` file in `paths`: a file is read directly, a
    /// directory is searched recursively (names starting with '.' are skipped) and its files
    /// are taken in byte-wise path order. A file that cannot be loaded is reported as an Error
    /// diagnostic (code "project.components.load_failed", message naming the path) and
    /// skipped; a missing path is a Warning ("project.components.missing_path"). A component
    /// whose id is already indexed is skipped with a Warning ("project.components.duplicate_id").
    core::Diagnostics scan(const std::vector<std::filesystem::path>& paths, const formats::LoadOptions& options = {});
    /// Adds one component (e.g. from a plugin). AlreadyExists when its id is indexed.
    core::Result<void> add(Component component, std::string source);
    void clear() { entries_.clear(); }

    const std::vector<ComponentEntry>& entries() const noexcept { return entries_; }
    /// Distinct non-empty categories / tags, sorted.
    std::vector<std::string> categories() const;
    std::vector<std::string> tags() const;
    /// Entries of one category ("" = uncategorised) / carrying one tag, in entries() order.
    std::vector<const ComponentEntry*> byCategory(std::string_view category) const;
    std::vector<const ComponentEntry*> byTag(std::string_view tag) const;
    /// By id, else the first entry (entries() order) with that name; null when none.
    const ComponentEntry* find(std::string_view idOrName) const;

private:
    void sort();
    std::vector<ComponentEntry> entries_;
};

} // namespace fos::project
