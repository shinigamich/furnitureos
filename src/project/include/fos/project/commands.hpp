// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Undoable commands on a Project (executed by Document, document.hpp). A command is plain data:
/// a UI label plus a function that mutates a project copy. Cabinet edits go through the atomic
/// editing API of the furniture module (furniture/edit.hpp).
#include "fos/core/result.hpp"
#include "fos/project/project.hpp"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace fos::project {

/// UI name of a command: `key` is an English text with {0}, {1}, ... placeholders (the app
/// translates the key with tr() and substitutes `args`), e.g.
/// key "Set {1} of {0} to {2}", args {"Base cabinet", "width", "900"}.
struct CommandLabel {
    std::string key;
    std::vector<std::string> args;
    /// The English text with the arguments substituted ("Set width of Base cabinet to 900").
    std::string english() const;
    bool operator==(const CommandLabel&) const = default;
};

/// What a command changed (for change notification and post-checks).
struct Affected {
    std::vector<std::string> items;    ///< Ids of furniture items whose definition / output may change (project order).
    std::vector<std::string> drawings; ///< Drawing ids whose stored edits changed (sorted).
    bool libraries = false;            ///< Materials / edge bands / hardware changed.
    bool settings = false;             ///< Metadata, cutting or project settings, custom data changed.
    bool operator==(const Affected&) const = default;
};

struct Command {
    CommandLabel label;
    /// Mutates `project` (a private copy). An error aborts the command.
    std::function<core::Result<Affected>(Project& project)> apply;
    /// Commands with the same non-empty key may be merged (Document::execute, Merge::WithPrevious).
    std::string mergeKey;
};

/// Factory functions. Item ids name Project::items entries; a missing id fails with NotFound.
namespace commands {

// --- parameters (cabinets and panels) ---
/// Constant value of a parameter (furniture::setParameter for cabinets; for panels the panel
/// graph). mergeKey "parameter:<item>:<name>".
Command setParameter(std::string itemId, std::string name, double value);
Command setParameterExpression(std::string itemId, std::string name, std::string expression);
/// Cabinets only (furniture::resetParameter).
Command resetParameter(std::string itemId, std::string name);

// --- cabinet structure (cabinets only; thin wrappers of furniture/edit.hpp) ---
Command setConstruction(std::string itemId, furniture::ConstructionOptions options);
Command setMaterials(std::string itemId, furniture::MaterialAssignment materials);
Command setHardware(std::string itemId, furniture::HardwareSelection hardware);
Command setPartOverride(std::string itemId, std::string slot, std::optional<furniture::PartOverride> override);
Command setInterior(std::string itemId, furniture::Section root);
Command replaceSection(std::string itemId, std::string path, furniture::Section section);
Command setShelfCount(std::string itemId, std::string path, int count);
Command setFront(std::string itemId, std::string path, std::optional<furniture::FrontSpec> front);
Command splitSection(std::string itemId, std::string path, furniture::SplitDirection direction,
                     std::vector<furniture::SectionSize> sizes);
Command setSectionSize(std::string itemId, std::string path, furniture::SectionSize size);

// --- panels ---
/// Replaces the whole panel definition (its id must stay the same part id or be unique).
Command setPanel(std::string itemId, furniture::PanelDefinition panel);

// --- items ---
/// Appends (or inserts before `position`, clamped) a new item; its id must be non-empty and unused.
Command addItem(Item item, std::optional<std::size_t> position = std::nullopt);
/// Removes the item and the stored edits of its assembly drawing.
Command removeItem(std::string itemId);
/// Copies an item under `newId` / `newName` with `placement`, inserted after the original.
/// A panel copy gets the part id "<newId>/panel". Drawing edits are not copied.
Command duplicateItem(std::string itemId, std::string newId, std::string newName, geometry::Transform placement);
/// Assembly frame -> project frame; must be rigid (geometry::Transform::isRigid).
Command setPlacement(std::string itemId, geometry::Transform placement);
Command renameItem(std::string itemId, std::string name);

// --- libraries ---
/// Adds the entry, or replaces the entry with the same id in place.
Command upsertMaterial(domain::Material material);
Command upsertEdgeBand(domain::EdgeBand edgeBand);
Command upsertHardware(domain::HardwareDefinition definition);
/// Fails (naming the items) while any item still references the entry.
Command removeMaterial(std::string materialId);
Command removeEdgeBand(std::string edgeBandId);
Command removeHardware(std::string hardwareId);

// --- drawings ---
/// Sets (or with nullopt removes) all stored edits of one drawing.
Command setDrawingEdits(std::string drawingId, std::optional<drawings::DrawingEdits> edits);
/// Sets (or with nullopt removes) the edit of one generated element of one drawing.
/// mergeKey "element:<drawing>:<element>".
Command setElementEdit(std::string drawingId, std::string elementId, std::optional<drawings::ElementEdit> edit);

// --- settings ---
/// materialId nullopt = project defaults; settings nullopt removes the material's own entry.
Command setCuttingSettings(std::optional<std::string> materialId, std::optional<CuttingSettings> settings);
Command setMetadata(Metadata metadata);
Command setSettings(ProjectSettings settings);
/// Must be a JSON object.
Command setCustom(formats::Json custom);

} // namespace commands
} // namespace fos::project
