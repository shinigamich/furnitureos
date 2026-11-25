// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Comparison of two projects (revisions): what changed from `before` (A) to `after` (B).
///
/// The PROJECT-LEVEL diff compares the saved data through the project JSON codecs
/// (codec.hpp: projectToJson), so every stored field is compared without a hand-written field
/// list: a field added to a codec is compared automatically. Array elements that are objects
/// with a string "id" (or, failing that, a string "name") are matched by that key, other arrays
/// element by element. Numbers are equal when they differ by at most DiffOptions::tolerance.
///
/// The optional MODEL-LEVEL diff resolves both projects (resolve.hpp) and compares the
/// generated parts by part id (finished size, material, edge bands, grain, operation count) and
/// the hardware quantities per hardware definition: "what changes in production".
/// Lengths are mm; placements map the item (assembly) frame into the project frame.
#include "fos/core/result.hpp"
#include "fos/formats/json.hpp"
#include "fos/project/project.hpp"

#include <optional>
#include <string>
#include <vector>

namespace fos::project {

struct DiffOptions {
    bool model = false;             ///< Also resolve both projects and compare the generated parts.
    double tolerance = 1e-6;        ///< Numbers (mm, money, ...) closer than this are equal.
    bool ignoreTimestamps = false;  ///< Do not compare metadata createdAt / modifiedAt.
};

/// One changed value. `path` is relative to the group it is listed in: "/" separated object
/// keys, array elements as "[id]" (matched by id or name) or "[index]" ("/trim/left",
/// "/remnants[r1]/length", "/children[1]/front/kind"). nullopt = the value is absent on that
/// side (added or removed key / element); JSON null is a present value.
struct ValueChange {
    std::string path;
    std::optional<formats::Json> before;
    std::optional<formats::Json> after;
};

/// A library entry present in both projects with different content.
struct LibraryEntryChange {
    std::string id;
    std::vector<ValueChange> fields; ///< Paths relative to the entry ("/thickness", "/color/r").
};

/// One library category ("materials", "edgeBands" or "hardware").
struct LibraryCategoryDiff {
    std::string category;
    std::vector<std::string> added;   ///< Ids only in B (B order).
    std::vector<std::string> removed; ///< Ids only in A (A order).
    std::vector<LibraryEntryChange> changed; ///< A order.
    bool empty() const { return added.empty() && removed.empty() && changed.empty(); }
};

/// A parameter whose stored definition changed. `before` / `after` are display texts of the
/// definition: the constant ("800") or "= <expression>", with " (kind ..., bounds ...)" details
/// when those changed; nullopt = the parameter does not exist on that side. Only stored
/// definitions are compared (derived values follow; see the model-level diff for effects).
struct ParameterChange {
    std::string name;
    std::optional<std::string> before;
    std::optional<std::string> after;
};

/// Placement change of an item (assembly frame -> project frame).
struct PlacementChange {
    bool moved = false;   ///< Origin differs by more than the tolerance.
    bool rotated = false; ///< An axis differs by more than the tolerance.
    geometry::Vec3 beforeOrigin;
    geometry::Vec3 afterOrigin;
};

/// Brief identification of an added / removed item.
struct ItemRef {
    std::string id;
    std::string name;
    std::string type; ///< "cabinet" or "panel".
};

/// An item present (same id) in both projects with different content.
struct ItemChange {
    std::string id;
    std::string name;              ///< Name in B.
    std::optional<std::string> renamedFrom; ///< Name in A when the name changed.
    std::optional<std::string> typeBefore;  ///< Set when the item type changed (cabinet <-> panel).
    std::vector<ParameterChange> parameters;
    std::vector<ValueChange> materials;     ///< Cabinet "materials" / panel "material" + "edges".
    std::vector<ValueChange> hardware;      ///< Cabinet hardware selection.
    std::vector<ValueChange> construction;  ///< Cabinet construction options.
    std::vector<ValueChange> interior;      ///< Section tree without fronts (paths below "/interior").
    std::vector<ValueChange> fronts;        ///< Front specs inside the section tree (paths below "/interior").
    std::optional<PlacementChange> placement;
    std::vector<ValueChange> other;         ///< Constraints, pinned parameters, contour, part id, role, ... (paths from the item).
};

/// A changed group of settings.
struct SettingsDiff {
    std::string group;                ///< "cutting", "pricing", "room" or "settings".
    std::vector<ValueChange> changes; ///< Paths relative to the group object.
};

// ---- model level ----------------------------------------------------------------------------

struct PartRef {
    std::string id;
    std::string name;
    double length = 0, width = 0, thickness = 0; ///< Finished size, mm.
    std::string material;
};

/// A part (same id) generated in both models with production-relevant differences.
struct PartChange {
    PartRef before;
    PartRef after;
    bool sizeChanged = false;     ///< Length, width or thickness.
    bool materialChanged = false;
    bool edgesChanged = false;    ///< Edge band on any of L1, L2, W1, W2.
    bool grainChanged = false;
    std::size_t operationsBefore = 0, operationsAfter = 0; ///< Machining operation counts.
};

/// Total quantity of one hardware definition over all assemblies (sum of instance quantities).
struct HardwareCountChange {
    std::string definition;
    int before = 0;
    int after = 0;
};

struct ModelDiff {
    std::vector<PartRef> added;     ///< Parts only in B (B model order).
    std::vector<PartRef> removed;   ///< Parts only in A (A model order).
    std::vector<PartChange> changed; ///< A model order.
    std::vector<HardwareCountChange> hardware; ///< Sorted by definition id; only differing counts.
    /// Items that could not be generated (missing from that model), so the comparison of their
    /// parts is not meaningful.
    std::vector<std::string> notGeneratedBefore, notGeneratedAfter;
    bool empty() const;
};

/// Everything that differs from A to B. Groups are in a fixed order; empty groups are omitted
/// from the vectors.
struct ProjectDiff {
    std::vector<ValueChange> metadata;          ///< Paths relative to "project" ("/name").
    std::vector<LibraryCategoryDiff> libraries; ///< Only non-empty categories, order materials, edgeBands, hardware.
    std::vector<ItemRef> itemsAdded;            ///< B order.
    std::vector<ItemRef> itemsRemoved;          ///< A order.
    std::vector<ItemChange> itemsChanged;       ///< A order.
    bool itemOrderChanged = false;              ///< Same common items, different relative order.
    std::vector<SettingsDiff> settings;         ///< Only non-empty groups.
    std::vector<ValueChange> other;             ///< drawingEdits, custom and unknown payload keys (paths from the payload).
    std::optional<ModelDiff> model;             ///< Set when DiffOptions::model.

    /// True when nothing differs (including the model diff when computed).
    bool empty() const;
};

/// Compares `before` (A) with `after` (B). Fails only with DiffOptions::model when a project
/// cannot be resolved (validateStructure errors, prefixed with "A: " / "B: "); an item that
/// cannot be generated is listed in ModelDiff::notGenerated* instead.
core::Result<ProjectDiff> diffProjects(const Project& before, const Project& after, const DiffOptions& options = {});

/// Model-level comparison of two resolved models (used by diffProjects).
ModelDiff diffModels(const domain::Model& before, const domain::Model& after, double tolerance = 1e-6);

/// Human-readable grouped report ("Metadata", "Libraries", "Items", "Settings", "Other",
/// "Production model"); "No differences.\n" when empty.
std::string diffText(const ProjectDiff& diff);

/// Machine-readable form: {"identical": bool, "metadata": [...], "libraries": [...],
/// "items": {"added", "removed", "changed", "orderChanged"}, "settings": [...], "other": [...],
/// "model": {...} (only when computed)}. A ValueChange is {"path", "before"?, "after"?}
/// (absent key = absent value).
formats::Json diffToJson(const ProjectDiff& diff);

} // namespace fos::project
