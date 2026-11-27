// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Inner-drawer editor view-model (furniture::InnerDrawerSpec, furniture README "Inner drawers"):
/// the editor state of a LEAF section behind a door, the one undoable section command that sets
/// or removes the drawers, the tree text and the generator findings of the item's inner drawers.
/// Nothing is computed here: the furniture module judges the spec (covering front, heights,
/// packers, hinge conflicts) and its refusal or findings are shown verbatim.
#include "fos/core/diagnostic.hpp"
#include "fos/core/result.hpp"
#include "fos/hardware/catalog.hpp"
#include "fos/project/commands.hpp"
#include "fos/project/resolve.hpp"

#include <QString>

#include <optional>
#include <string>

namespace fos::app {

/// What the "Inner drawers" group shows / submits.
struct InnerDrawerInput {
    bool enabled = false;
    /// Compartment heights bottom to top in the drawer-heights syntax (parseSizeList), e.g. "1*; 1*".
    QString heights = QStringLiteral("1*; 1*");
    std::string slide = std::string(hardware::ids::kSlideBallBearing); ///< DrawerSlide hardware id.
    furniture::DrawerBottomMount bottom = furniture::DrawerBottomMount::Grooved;
    furniture::JointFastener joint = furniture::JointFastener::Dowel;
    furniture::InnerDrawerFront front = furniture::InnerDrawerFront::Box;
    furniture::InnerFaciaHandle handle = furniture::InnerFaciaHandle::CutOut; ///< Facia only.
};

/// Editor state of an existing spec (enabled = false and the defaults for std::nullopt).
InnerDrawerInput innerDrawerInput(const std::optional<furniture::InnerDrawerSpec>& spec);
/// The spec the editor submits: std::nullopt when not enabled; InvalidArgument for an
/// unparsable height list (parseSizeList message).
core::Result<std::optional<furniture::InnerDrawerSpec>> innerDrawerSpec(const InnerDrawerInput& input);
/// commands::replaceSection of the leaf at `path` with its inner drawers set (removed when not
/// enabled); size, shelves, rail and front of the section are kept (the engine refuses drawers
/// together with shelves or a rail). InvalidArgument when the section does not exist or is split,
/// or for an unparsable height list.
core::Result<project::Command> innerDrawersCommand(const furniture::CabinetDefinition& cabinet,
                                                   const std::string& itemId, const std::string& path,
                                                   const InnerDrawerInput& input);

/// Front covering the section at `path`: its own front, otherwise that of the nearest enclosing
/// section; std::nullopt when the section is open or does not exist.
std::optional<furniture::FrontKind> coveringFront(const furniture::Section& root, const std::string& path);
/// True when the section at `path` is a leaf behind a Door, DoorPair or SlidingDoors (the
/// covering fronts the furniture module accepts for inner drawers), or already has inner drawers
/// (so they can be edited or removed). The remaining conditions are judged by the engine.
bool innerDrawersEditable(const furniture::Section& root, const std::string& path);

/// Translated tree text, e.g. "inner drawers: 2" or "inner drawers: 3 with facias".
QString innerDrawersText(const furniture::InnerDrawerSpec& spec);

/// Warning / Error generator findings of the item's inner drawers (codes "furniture.inner_drawers.*",
/// e.g. furniture.inner_drawers.hinge_conflict), in resolve order. Empty when the item is missing.
core::Diagnostics innerDrawerFindings(const project::Resolved& resolved, const std::string& itemId);

} // namespace fos::app
