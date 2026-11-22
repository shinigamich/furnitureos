// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Direct modelling (phase 15): which envelope faces of an item can be dragged in the 3D view
/// and the ONE undoable command a drag turns into. Values come from the parameter graph and the
/// resolved model; the furniture generator decides whether a size is valid.
///
/// Handles: +X / -X (cabinet width, panel length or width), +Y / -Y (cabinet depth, panel width),
/// +Z (cabinet height, panel length or width). -Z is never offered (items stand on their
/// placement). A -X / -Y drag keeps the OPPOSITE face where it is: the parameter changes by
/// delta and the item placement moves by -delta along that assembly axis.
///
/// ONE UNDO STEP: resizeCommand() builds a single combined project::Command whose apply() runs
/// commands::setParameter(...).apply and then commands::setPlacement(...).apply on the same
/// project copy (Document applies commands to a private copy, so a refusal of either part leaves
/// the project unchanged). Document merging is not used: the two commands have different merge
/// keys and merging is meant for repeated edits of one value. The combined command keeps the
/// label of the parameter change ("Set width of ... to 900") and has no merge key.
#include "fos/core/result.hpp"
#include "fos/geometry/aabb.hpp"
#include "fos/geometry/transform.hpp"
#include "fos/project/commands.hpp"

#include <QString>

#include <optional>
#include <string>
#include <vector>

namespace fos::app {

class Session;

enum class ResizeAxis { X, Y, Z };

/// One draggable face of an item's envelope.
struct ResizeHandle {
    ResizeAxis axis = ResizeAxis::X; ///< Assembly-frame axis.
    bool negative = false;           ///< The face at the minimum of `axis` (-X / -Y).
    std::string parameter;           ///< "width", "height", "depth" (cabinet) / "length", "width" (panel).
    double value = 0;                ///< Current parameter value, mm.
};

/// What the viewport needs to show the handles of one item.
struct ResizeTarget {
    std::string itemId;
    geometry::Transform placement; ///< Assembly frame -> project frame (Item::placement()).
    /// Envelope in the ASSEMBLY frame, mm. Cabinet: [0,width] x [0,depth] x [0,height] from the
    /// parameters (nominal envelope, ARCHITECTURE.md 4.2). Panel: the box of its generated part.
    geometry::Aabb box;
    std::vector<ResizeHandle> handles; ///< Order: +X, -X, +Y, -Y, +Z (those that apply).
};

/// Handles of item `itemId`, or nullopt when the item does not exist or is not generated in
/// the session's resolved model (nothing to drag).
std::optional<ResizeTarget> resizeTarget(const Session& session, const std::string& itemId);

/// The parameter a drag along `axis` changes for `item`, or nullopt (e.g. a panel's thickness
/// axis). Panels by orientation: Horizontal X=length Y=width; VerticalSide Z=length Y=width;
/// VerticalFront X=length Z=width.
std::optional<std::string> resizeParameter(const project::Item& item, ResizeAxis axis);

/// The combined command (see file comment). Errors: unknown item (NotFound), an axis without a
/// parameter or -Z (InvalidArgument), a non-finite value (InvalidArgument). Size limits are
/// judged when the command is executed (furniture/parametric messages).
core::Result<project::Command> resizeCommand(const project::Project& project, const std::string& itemId,
                                             ResizeAxis axis, bool negative, double newValue);

/// Builds and executes resizeCommand(); on error the project is unchanged and the error carries
/// the core message unchanged (shown in the status bar by the window).
core::Result<void> applyResize(Session& session, const std::string& itemId, ResizeAxis axis, bool negative,
                               double newValue);

/// Short translated dimension letter for the in-view label: width "W", height "H", depth "D",
/// length "L" (other names returned unchanged).
QString resizeLabel(const std::string& parameter);

} // namespace fos::app
