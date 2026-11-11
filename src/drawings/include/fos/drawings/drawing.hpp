// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/core/result.hpp"
#include "fos/domain/model.hpp"
#include "fos/drawings/edits.hpp"
#include "fos/drawings/part_view.hpp"
#include "fos/drawings/render.hpp"
#include "fos/drawings/sheet.hpp"

#include <string>
#include <vector>

/// One-call drawing generation. The model is taken by const reference: generating
/// or editing a drawing can never modify it.
namespace fos::drawings {

/// Arrangement of the standard three views (ISO 5456-2).
enum class ProjectionMethod {
    /// ISO "first angle" (European practice, default): TOP view BELOW the front view,
    /// LEFT-side view to the RIGHT of the front view.
    FirstAngle,
    /// "Third angle" (US practice): TOP view ABOVE the front view, RIGHT-side view to
    /// the RIGHT of the front view.
    ThirdAngle
};

/// A section view on its own sheet.
struct SectionRequest {
    ViewKind direction = ViewKind::Left; ///< Viewing direction; the plane is perpendicular to it.
    double position = 0;                 ///< See ViewOptions::sectionPosition.
    std::string id;                      ///< View id; empty = "<assembly-id>/section.<kind>".
};

struct AssemblyDrawingOptions {
    SheetOptions sheet; ///< sheet.title.title empty -> the assembly name is used.
    DrawingStyle style;
    ProjectionMethod projection = ProjectionMethod::FirstAngle;
    bool topView = true;
    bool sideView = true;
    HiddenLineMode hiddenLines = HiddenLineMode::Dashed;
    bool autoDimensions = true;
    AutoDimensionOptions dimensions;
    double viewGap = 12.0; ///< Paper mm between the annotated boxes of neighbouring views.
    std::vector<SectionRequest> sections;
};

struct PartDrawingOptions {
    SheetOptions sheet; ///< sheet.title.title empty -> the part name is used.
    DrawingStyle style;
    PartViewOptions view;
};

/// A view with the annotations (edits applied) that were rendered onto a sheet.
struct GeneratedView {
    View view;
    ViewAnnotations annotations;
    std::size_t sheetIndex = 0;
};

struct DrawingResult {
    std::vector<Sheet> sheets;
    std::vector<GeneratedView> views;
    EditReport edits;
    core::Diagnostics diagnostics; ///< View diagnostics (e.g. HLR fallbacks) followed by edits.diagnostics.
};

/// Sheet 1: front view plus (optionally) top and side views of the assembly, aligned
/// with the front view and arranged per `projection`. One further sheet per section.
/// Sheets are numbered from sheet.title.sheetNumber; the printed total is
/// max(sheet.title.sheetCount, last number).
///
/// Scale: with ScaleRequest::Mode::Explicit the given scale is used and the call
/// fails with ConstraintViolation (message states required and available size) when
/// the views do not fit; with FitToPage each sheet gets the largest standard scale
/// that fits (recorded in Sheet::scale and the title block) and the call fails when
/// even 1:100 does not fit.
/// Fails with NotFound when the assembly does not exist or has no drawable part.
core::Result<DrawingResult> generateAssemblyDrawing(const domain::Model& model, const domain::AssemblyId& assemblyId,
                                                    const AssemblyDrawingOptions& options = {},
                                                    const DrawingEdits& edits = {});

/// One sheet with the part view (see part_view.hpp), centred in the drawing area.
/// The part is searched in all assemblies of the model (first match).
core::Result<DrawingResult> generatePartDrawing(const domain::Model& model, const domain::PartId& partId,
                                                const PartDrawingOptions& options = {},
                                                const DrawingEdits& edits = {});

} // namespace fos::drawings
