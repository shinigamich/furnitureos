// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Edit > Add panel…: a free panel item (project::PanelItem) of a given size, orientation,
/// material and edge bands, placed at the origin, at the selection, next to a selected item's
/// face, or filling the gap between two selected items.
///
/// Frame and units: project frame, mm. The new panel item has an axis-aligned placement (a pure
/// translation to the panel's minimum corner) and its x / y / z parameters are 0, so the panel's
/// box in the project frame is [corner, corner + size] with size by orientation
/// (furniture::generatePanel):
///   Horizontal:    (length, width, thickness)
///   VerticalSide:  (thickness, width, length)
///   VerticalFront: (length, thickness, width)
/// The thickness is the material's thickness (project libraries). Reference boxes are the
/// generated assemblies' boxes (edit_tools.hpp itemBounds()).
#include "fos/app/edit_tools.hpp"
#include "fos/furniture/enums.hpp"

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace fos::app {

class Session;

/// Where the new panel goes.
enum class PanelPlacement {
    Origin,       ///< Minimum corner at (0, 0, 0) of the project frame.
    AtSelection,  ///< Minimum corner at the minimum corner of the reference items' box.
    OnTop,        ///< On the top face of the reference box (same min x / y, z = box max z).
    Below,        ///< Under the bottom face (z = box min z - panel height).
    LeftOf,       ///< Left of the box (x = box min x - panel size x), same min y / z.
    RightOf,      ///< Right of the box (x = box max x), same min y / z.
    InFront,      ///< In front of the box (y = box min y - panel size y), same min x / z.
    Behind,       ///< Behind the box (y = box max y), same min x / z.
    FillGap,      ///< Fills the free space between exactly two reference items (see planAddPanel).
};

struct AddPanelRequest {
    std::string name;  ///< Item / part name; empty = "Panel" made unique.
    double length = 600;  ///< Finished length, mm (ignored along the axes FillGap sizes).
    double width = 300;   ///< Finished width, mm (ditto).
    furniture::PanelOrientation orientation = furniture::PanelOrientation::Horizontal;
    std::string materialId;  ///< Must exist in the project libraries.
    /// Edge band ids indexed by domain::EdgeSide (L1, L2, W1, W2); nullopt = raw edge.
    std::array<std::optional<std::string>, 4> edges{};
    PanelPlacement placement = PanelPlacement::Origin;
    /// Reference items (the selection): >= 1 for the face placements, exactly 2 for FillGap.
    std::vector<std::string> referenceIds;
    /// FillGap only: position of the panel along its thickness axis, measured from the start of
    /// the gap region on that axis, mm (0 = flush with the region's minimum).
    double thicknessOffset = 0;
};

/// The computed panel: final size and minimum corner.
struct AddPanelPlan {
    double length = 0, width = 0, thickness = 0;  ///< mm.
    geometry::Vec3 minCorner;                     ///< Project frame, mm.
    geometry::Vec3 size;                          ///< Box extents along X / Y / Z, mm.
};

/// Box extents (X, Y, Z) of a panel of the given size and orientation.
geometry::Vec3 panelBoxSize(furniture::PanelOrientation orientation, double length, double width, double thickness);

/// Computes size and position. FillGap: the two reference boxes must be separated along one
/// axis g (the axis with the largest positive clearance hi.min - lo.max) and overlap by more
/// than 0 along the two others; the gap region is [lo.max, hi.min] along g and the overlap along
/// the others. The panel's length and width take the region's extents along the two
/// non-thickness axes of `orientation` (so a horizontal panel between two sides becomes a shelf
/// of the clear width); along the thickness axis it starts at region min + thicknessOffset and
/// must fit inside the region (InvalidArgument naming the numbers otherwise).
/// Errors: unknown material (NotFound), non-positive / non-finite sizes, wrong number of
/// references, a reference without generated geometry, references that touch or overlap.
core::Result<AddPanelPlan> planAddPanel(const project::Project& project, const project::Resolved* resolved,
                                        const AddPanelRequest& request);

/// The new panel item for `plan` (id `itemId`, part id "<itemId>/panel", unique name).
core::Result<project::Item> makePanelItem(const project::Project& project, const std::string& itemId,
                                          const AddPanelRequest& request, const AddPanelPlan& plan);

/// Plans, adds the item (commands::addItem, ONE undo step labelled "Add panel {0}") and selects
/// it. Returns the new item id (nextItemId(project, "panel")).
core::Result<std::string> applyAddPanel(Session& session, const AddPanelRequest& request);

} // namespace fos::app
