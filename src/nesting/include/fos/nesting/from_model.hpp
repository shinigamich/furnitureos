// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/core/diagnostic.hpp"
#include "fos/domain/model.hpp"
#include "fos/nesting/types.hpp"

#include <string>
#include <vector>

/// Adapter from a domain::Model to true-shape nesting jobs for the SHAPED parts (ADR-0008).
///
/// Target: fos::nesting_model (deps fos::nesting, fos::domain). It lives in src/nesting rather than
/// in fos::cutting_model because ARCHITECTURE.md orders `nesting` after `cutting`: cutting code may
/// not depend on nesting. The guillotine side of the routing split is
/// fos::cutting::cuttingFromModel() (fos/cutting/from_model.hpp); both use the same predicate
/// `!domain::isRectangularOutline(part)`, so every part goes to exactly one of the two.
namespace fos::nesting {

/// Shop settings for nesting jobs derived from a model. mm.
struct ModelSettings {
    double toolDiameter = 8.0;  ///< Router bit that cuts the parts out, > 0.
    double clearance = 0.0;     ///< Extra distance between parts beyond one tool diameter, >= 0.
    /// Maximum distance between an exact arc and its tessellation (chord tolerance), > 0. Arcs
    /// are tessellated with vertices ON the arc (geometry::tessellate), so a convex arc can bulge
    /// up to this much beyond its polygon; the spacings below add it so that the EXACT outlines
    /// keep the required distance.
    double chordTolerance = 0.1;
    Margins margins{10.0, 10.0, 10.0, 10.0}; ///< Per sheet side (clamping / damaged edges).
    double minRemnantLength = 300.0;
};

/// How a nesting part relates to its domain part.
struct NestedPartFrame {
    std::string partId;
    /// The nesting outline is the RAW part-frame outline rotated by this angle (0 or 90 degrees,
    /// CCW about the part-frame origin, no translation). 90 for grain AlongWidth on a grained
    /// material, whose width must run along the sheet length. A placement therefore maps a
    /// part-frame point p to  R(placement.rotationDeg) * R(preRotationDeg) * p + translation.
    double preRotationDeg = 0;
    double exactArea = 0;    ///< Exact area of the raw outline (signedArea of cutContour), mm^2.
    double exactNetArea = 0; ///< exactArea minus the exact cut-out areas, mm^2.
};

/// One nesting job per material that has at least one shaped part.
struct MaterialNestJob {
    std::string materialId;
    std::string materialName;
    double thickness = 0;
    Job job;
    std::vector<NestedPartFrame> frames; ///< Parallel to job.parts.
};

/// Builds one nesting Job per material (Model::materials order) from every part with
/// `!domain::isRectangularOutline(part)` (assemblies and parts in model order); rectangular parts
/// are ignored (they go to guillotine cutting).
///
/// * Outline = domain::cutContour(part, model) (the RAW outline before edge banding),
///   tessellated with settings.chordTolerance; cut-outs tessellated the same way into
///   Part::holes (data only). One nesting Part per domain part, quantity 1, id = part id,
///   label = part name (id if empty), mirrorAllowed = false.
/// * Rotation from domain::sheetAlignment(part, &material): Free -> Quarter;
///   LengthAlongSheet -> Half (0/180, keeps the grain); LengthAcrossSheet -> outline pre-rotated
///   by 90 degrees (NestedPartFrame::preRotationDeg) and Half.
/// * Job::partSpacing = toolDiameter + clearance + 2 * chordTolerance (one router channel between
///   two parts, plus clearance, plus the tessellation allowance of both outlines);
///   Job::edgeSpacing = toolDiameter + chordTolerance (the cut stays inside the margin line).
/// * Stock: one unlimited sheet "<material id>/sheet" of the material's sheet size with
///   settings.margins.
///
/// Diagnostics (never dropped silently; the part gets no nesting entry on Error):
///   nesting.adapter.invalid_settings (Error, once; no jobs at all) tool diameter / tolerance <= 0,
///                                     clearance < 0 or non-finite values;
///   nesting.adapter.material_missing (Error)   material not in the model;
///   nesting.adapter.invalid_sheet (Error)      sheet size not positive;
///   nesting.adapter.invalid_shape (Error)      cutContour() fails (message names the part);
///   nesting.adapter.too_many_vertices (Error)  tessellation exceeds the nesting limit of 1000
///                                              vertices (use a larger chord tolerance);
///   nesting.adapter.thickness_mismatch (Warning) part thickness differs from the material's.
std::vector<MaterialNestJob> nestingJobsFromModel(const domain::Model& model, const ModelSettings& settings,
                                                  core::Diagnostics& diagnostics);

} // namespace fos::nesting
