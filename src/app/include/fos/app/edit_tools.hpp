// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Free-modelling editing tools on whole items (cabinets and free panels): grouped commands,
/// move / rotate, mirror, array, align, distribute, multi-item delete and duplicate.
///
/// Frame and units: the PROJECT frame (X right, Y depth away from the front, Z up), mm.
/// Every box used here is the axis-aligned bounding box of an item's GENERATED assembly in the
/// project frame (project::Resolved), so an item that failed to generate cannot be transformed
/// by box (NotFound naming it). Each tool returns ONE project::Command (several item changes are
/// grouped with groupCommands()), so every user action is exactly one undo step. Nothing here
/// computes manufacturing data.
#include "fos/core/result.hpp"
#include "fos/geometry/aabb.hpp"
#include "fos/geometry/transform.hpp"
#include "fos/project/commands.hpp"
#include "fos/project/resolve.hpp"

#include <optional>
#include <string>
#include <vector>

namespace fos::app {

class Session;

enum class Axis { X = 0, Y = 1, Z = 2 };

/// One command applying `commands` in order to the same project copy (all or nothing: the
/// first error aborts the group and is returned). Affected = union of the parts' Affected
/// (items in project order after the change, followed by removed ids in first-seen order;
/// drawings sorted). An empty list gives a command that changes nothing.
project::Command groupCommands(project::CommandLabel label, std::vector<project::Command> commands);

/// Bounding box of item `itemId`'s generated assembly in the project frame; nullopt when the
/// item has no generated assembly (unknown or refused by the generator).
std::optional<geometry::Aabb> itemBounds(const project::Resolved& resolved, const std::string& itemId);
/// Union of itemBounds() of `itemIds`. NotFound naming the first item without an assembly;
/// InvalidArgument for an empty list.
core::Result<geometry::Aabb> itemsBounds(const project::Resolved& resolved, const std::vector<std::string>& itemIds);

/// Exact rotation by `degrees` (counter-clockwise looking against the axis) about `axis` through
/// `pivot`; multiples of 90 degrees use exact 0 / +-1 matrix entries (no cos/sin round-off).
geometry::Transform rotationAbout(Axis axis, double degrees, geometry::Vec3 pivot);

// --- move / rotate -------------------------------------------------------------------------

/// setPlacement of every item translated by `delta` (project frame). Label "Move {0}" (one item)
/// or "Move {0} items". NotFound for an unknown id, InvalidArgument for an empty list, a repeated
/// id or a non-finite delta.
core::Result<project::Command> translateItemsCommand(const project::Project& project,
                                                     const std::vector<std::string>& itemIds, geometry::Vec3 delta);
/// Moves the items rigidly so the minimum corner of their union box lands on `minCorner`.
core::Result<project::Command> moveItemsToCommand(const project::Project& project, const project::Resolved& resolved,
                                                  const std::vector<std::string>& itemIds, geometry::Vec3 minCorner);
/// Rotates the items rigidly by `degrees` about `axis` through the centre of their union box.
/// About X or Y only free panels may be rotated (InvalidArgument naming a cabinet: a cabinet on
/// its side is not a furniture position). Non-finite angle: InvalidArgument.
core::Result<project::Command> rotateItemsCommand(const project::Project& project, const project::Resolved& resolved,
                                                  const std::vector<std::string>& itemIds, Axis axis, double degrees);

// --- copies ---------------------------------------------------------------------------------

/// Result of a tool that creates copies.
struct CopyPlan {
    project::Command command;
    std::vector<std::string> newIds;     ///< Ids of the copies (in creation order).
    /// Copies that are placed mirrored but whose content is NOT reflected (cabinets, shaped
    /// panels, panels not aligned with the mirror plane): a reflection is not a rigid placement,
    /// so these are translated copies whose box is the mirror image of the original's box.
    std::vector<std::string> notReflected;
};

/// Fresh ids for `count` copies of `sourceId` (project::freeItemId, also avoiding the ids
/// returned before) and the matching unique names ("<name> 2", "<name> 3", ...).
std::vector<std::pair<std::string, std::string>> freshCopyNames(const project::Project& project,
                                                                const std::string& sourceId, int count,
                                                                std::vector<std::string>* usedIds = nullptr,
                                                                std::vector<std::string>* usedNames = nullptr);

/// Mirror copies across the plane through the centre of the items' union box whose normal is
/// `normal` (Axis::X = the YZ plane, flips X). Each copy keeps the original's orientation and
/// is placed so its box is the reflection of the original's box: new min_a = 2 c_a - max_a.
/// A rectangular free panel whose edges are aligned with the plane is reflected EXACTLY: its
/// edge bands on the two edges facing along the normal are swapped (L1 <-> L2 when the panel
/// width runs along the normal, W1 <-> W2 for the length); all other items go to notReflected.
core::Result<CopyPlan> mirrorItemsCommand(const project::Project& project, const project::Resolved& resolved,
                                          const std::vector<std::string>& itemIds, Axis normal);
/// Linear array: `count` (1 ..= 500) further copies of each item, copy k translated by
/// k * step (k = 1 .. count). InvalidArgument for a bad count, a zero or non-finite step.
core::Result<CopyPlan> arrayItemsCommand(const project::Project& project, const std::vector<std::string>& itemIds,
                                         int count, geometry::Vec3 step);
/// Copies of every item (selection order) shifted together along +X so the group's box starts
/// at the right edge of all items (Session::rightEdge() semantics).
core::Result<CopyPlan> duplicateItemsCommand(const project::Project& project, const project::Resolved& resolved,
                                             const std::vector<std::string>& itemIds);
/// removeItem of every item (one step). NotFound / InvalidArgument as translateItemsCommand.
core::Result<project::Command> removeItemsCommand(const project::Project& project,
                                                  const std::vector<std::string>& itemIds);

// --- align / distribute -----------------------------------------------------------------------

/// Box face used by alignItemsCommand: Left = min X, Right = max X, Front = min Y (front of
/// the furniture), Back = max Y, Bottom = min Z, Top = max Z.
enum class AlignEdge { Left, Right, Front, Back, Bottom, Top };

/// Translates every item after the first along one axis so that its `edge` coordinate equals
/// the first item's. Needs >= 2 items (InvalidArgument). Items already aligned produce no change.
core::Result<project::Command> alignItemsCommand(const project::Project& project, const project::Resolved& resolved,
                                                 const std::vector<std::string>& itemIds, AlignEdge edge);
/// Distributes >= 3 items along `axis` with equal free gaps: items sorted by box minimum (ties:
/// selection order); the first and last stay; gap = (last.max - first.min - sum of sizes) /
/// (n - 1), which is negative (equal overlaps) when the items do not fit.
core::Result<project::Command> distributeItemsCommand(const project::Project& project,
                                                      const project::Resolved& resolved,
                                                      const std::vector<std::string>& itemIds, Axis axis);

// --- session helpers (execute the command; one undo step; select the result) ----------------

/// Executes `command` on the session (error when it cannot be resolved first).
core::Result<void> applyItemsCommand(Session& session, const core::Result<project::Command>& command);
/// Executes plan.command and selects the copies.
core::Result<std::vector<std::string>> applyCopyPlan(Session& session, const core::Result<CopyPlan>& plan);
/// Deletes every selected item (one step) and clears the selection.
core::Result<void> deleteSelectedItems(Session& session);
/// Duplicates every selected item (one step) and selects the copies.
core::Result<std::vector<std::string>> duplicateSelectedItems(Session& session);
/// Moves the selected items by (dx, dy, dz) (plan-view arrow keys: 1 mm, Shift = 10 mm).
core::Result<void> nudgeSelectedItems(Session& session, geometry::Vec3 delta);

} // namespace fos::app
