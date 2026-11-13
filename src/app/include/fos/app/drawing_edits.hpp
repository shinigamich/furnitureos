// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Drawing annotation editor view-model: picking dimensions on a generated sheet (by the
/// `elementId` of the drawings module's primitives and the sheet's view placements),
/// snapping to part corners, and the project commands that store the edits
/// (commands::setElementEdit for generated elements, commands::setDrawingEdits for user
/// dimensions). Text overrides record the measured value through drawings::setTextOverride /
/// resolveUserDimension so the drawings module can flag them when the model changes.
#include "fos/app/drawing_request.hpp"
#include "fos/project/commands.hpp"

#include <QString>

#include <optional>
#include <string>
#include <vector>

namespace fos::app {

/// Key of the request's stored edits: project::assemblyDrawingId(id) or partDrawingId(id).
std::string drawingIdOf(const DrawingRequest& request);
/// Model geometry of the requested drawing (drawn assembly or drawn part; null pointers when
/// it is not in the resolved model).
drawings::EditModelContext editContext(const project::Resolved& resolved, const DrawingRequest& request);

/// A dimension of a generated drawing, as drawn (edits applied).
struct DimensionRef {
    std::size_t viewIndex = 0; ///< Index in DrawingResult::views.
    drawings::Dimension dimension;
    bool user = false; ///< A user dimension (stored in DrawingEdits::dimensions).
};
std::optional<DimensionRef> findDimension(const drawings::DrawingResult& drawing, const std::string& id);

/// Index in DrawingResult::views of the view on sheet `sheetIndex` whose paper bounds
/// contain `paper` (paper mm, origin bottom-left), the nearest one when several do.
std::optional<std::size_t> viewAt(const drawings::DrawingResult& drawing, std::size_t sheetIndex,
                                  geometry::Vec2 paper);
/// Id of the visible dimension whose drawn primitives (lines, arrows, text box) pass closest
/// to `paper`, when that distance is at most `tolerance` paper mm; empty otherwise.
std::string dimensionAt(const drawings::DrawingResult& drawing, std::size_t sheetIndex, geometry::Vec2 paper,
                        double tolerance = 1.5);
/// Paper box of every primitive of `elementId` on the sheet (empty rectangle when none).
drawings::Rect2 elementBounds(const drawings::Sheet& sheet, const std::string& elementId);

/// The part corner nearest to a view point, found by resolving every corner (0..7) of every
/// part of the drawn assembly (or the drawn part) with drawings::resolveAnchor.
struct CornerPick {
    drawings::ModelAnchor anchor;
    geometry::Vec2 viewPoint; ///< Resolved corner, view coordinates (model mm).
    double distance = 0;      ///< From the query point, model mm.
    std::string partName;
};
std::optional<CornerPick> nearestCorner(const drawings::GeneratedView& view,
                                        const drawings::EditModelContext& model, geometry::Vec2 viewPoint);

// --- commands --------------------------------------------------------------------------
/// New paper offset (mm, signed as drawings::Dimension::offset) of the dimension.
project::Command dimensionOffsetCommand(const project::Project& project, const std::string& drawingId,
                                        const DimensionRef& dimension, double offset);
/// Hides a generated dimension (ElementEdit::hidden). A user dimension cannot be hidden, only
/// removed (InvalidArgument).
core::Result<project::Command> hideDimensionCommand(const project::Project& project,
                                                    const std::string& drawingId,
                                                    const DimensionRef& dimension);
/// Text override (empty text = remove the override). The measured value at the time of the
/// edit is recorded (drawings::setTextOverride / UserDimension::measuredAtEdit).
project::Command dimensionTextCommand(const project::Project& project, const std::string& drawingId,
                                      const DimensionRef& dimension, const QString& text);
/// Removes the stored edit `id` of the drawing: an element edit, or a user annotation.
/// NotFound when the drawing has no such edit.
core::Result<project::Command> removeEditCommand(const project::Project& project,
                                                 const std::string& drawingId, const std::string& id);
/// Adds a user dimension between two model anchors in `view` (id "user.dim.<n>", the
/// smallest unused n). Refused (InvalidArgument, the drawings module's reason) when an
/// anchor cannot be resolved, or when both points coincide along the measured direction.
core::Result<project::Command>
addUserDimensionCommand(const project::Project& project, const std::string& drawingId,
                        const drawings::GeneratedView& view, const drawings::EditModelContext& model,
                        drawings::DimensionOrientation orientation, const drawings::ModelAnchor& first,
                        const drawings::ModelAnchor& second, double offset);

// --- edit list and findings --------------------------------------------------------------
struct DrawingEditRow {
    enum class Kind { Element, UserDimension, UserText, UserLeader, UserDetailMarker };
    Kind kind = Kind::Element;
    std::string id;
    bool hidden = false;
    std::optional<double> offset;
    std::optional<std::string> text; ///< Text override (dimensions) or the annotation's text.
    bool stale = false;              ///< drawings.edits.stale_override reported for it.
    bool orphan = false;             ///< In DrawingResult::edits.orphanIds.
};
/// Every stored edit of the drawing (element edits by id, then user annotations in stored
/// order), flagged from the generated drawing's EditReport (`drawing` may be null).
std::vector<DrawingEditRow> drawingEditRows(const project::Project& project, const std::string& drawingId,
                                            const drawings::DrawingResult* drawing);
/// Translated one-line summary ("hidden", "offset 12 mm", "text “800” — STALE", "ORPHAN").
QString editRowText(const DrawingEditRow& row);
/// The edit findings of the drawing: drawings.edits.* diagnostics (stale overrides, orphans,
/// bad anchors, not applicable, duplicate ids), in report order.
core::Diagnostics editFindings(const drawings::DrawingResult& drawing);

} // namespace fos::app
