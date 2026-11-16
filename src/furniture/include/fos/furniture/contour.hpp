// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Generation of a part's domain::PartShape from a ContourSpec (contour_spec.hpp) and the
/// check of its machining against the outline. Rules (README "Contoured parts"):
///
/// * The part keeps its construction-derived size L x W; the outline's exact bounding box is
///   [0,L]x[0,W] (ADR-0008). A Convex ArcEdge is cut INTO the box (apex on the edge line).
/// * Edge bands: the part's four L1/L2/W1/W2 bands (generator roles plus overrides) are moved
///   onto the outline segments: a straight segment on a box side takes that side's band; an
///   ArcEdge arc takes the band of the edge it replaces; a corner arc of RoundedCorners takes
///   the band of its L side (L1 or L2) when that is banded, otherwise of its W side; Custom
///   segments follow ContourVertexSpec::bandFrom. Part::edges is then cleared and
///   `shape->outerEdges` holds one entry per segment (empty vector when all are raw).
///   Cut-outs are never banded.
#include "fos/core/diagnostic.hpp"
#include "fos/core/result.hpp"
#include "fos/domain/part.hpp"
#include "fos/furniture/contour_spec.hpp"
#include "fos/parametric/expression.hpp"

#include <optional>
#include <string>

namespace fos::furniture {

/// Read-only names of the shaped part available in contour expressions (mm).
namespace contour_param {
inline constexpr const char* kLength = "contour.length";
inline constexpr const char* kWidth = "contour.width";
inline constexpr const char* kThickness = "contour.thickness";
} // namespace contour_param

namespace limits {
/// Vertices of one Custom outline.
inline constexpr std::size_t kMaxContourVertices = 500;
/// Cut-outs of one contour.
inline constexpr std::size_t kMaxContourCutouts = 50;
/// Sum of contourSegmentEstimate() over all part contours of one cabinet (bounds the
/// quadratic contour validation of a whole cabinet).
inline constexpr std::size_t kMaxContourSegmentsPerCabinet = 10000;
} // namespace limits

/// Upper bound of the segments a spec produces: Custom = its vertices, other kinds 8, plus 8
/// per cut-out.
std::size_t contourSegmentEstimate(const ContourSpec& spec) noexcept;

/// Structural check (also run by checkDefinitionLimits for every override contour) that needs no values: the kind's expressions are present and parse
/// (RoundedCorners: empty radius = 0; ArcEdge: a rise; Custom: at least 3 vertices, at most
/// limits::kMaxContourVertices, x / y present, bulges finite with |bulge| <= 1; cut-outs at most
/// limits::kMaxContourCutouts, each with its expressions). `label` names the owner in messages,
/// e.g. "part 'shelf.1' of cabinet 'cab'". InvalidArgument / ParseError otherwise.
core::Result<void> checkContourSpec(const ContourSpec& spec, const std::string& label);

/// Builds the shape of `part` (uses its length, width, thickness and its four `edges`).
/// `parameters` resolves graph names (the contour_param names are added on top).
/// Returns std::nullopt for a Rectangle without cut-outs (the part stays a plain rectangle).
/// Errors (messages start with "furniture.contour.invalid:" and name `label`, the field and
/// the expression): checkContourSpec failures, an expression that cannot be evaluated, radii
/// that do not fit a side (naming both corners, their sum and the side length), a rise out of
/// range, a Custom outline that is not counter-clockwise, self-intersects or does not span
/// exactly [0,L]x[0,W], a cut-out that is invalid, not strictly inside the outline or touching
/// another cut-out.
core::Result<std::optional<domain::PartShape>> buildPartShape(const ContourSpec& spec, const domain::Part& part,
                                                              const parametric::Lookup& parameters,
                                                              const std::string& label);

/// buildPartShape, then sets `part.shape` (when not nullopt) and clears `part.edges`.
/// On error `part` is unchanged.
core::Result<void> applyContour(domain::Part& part, const ContourSpec& spec, const parametric::Lookup& parameters,
                                const std::string& label);

/// Machining that does not lie within the finished outline of a SHAPED part (parts without a
/// shape yield nothing): one Error `furniture.contour.machining_outside` per operation whose
/// footprint in the part's XY plane leaves the outer contour or enters a cut-out. Footprints:
/// face A/B hole = its circle; edge-face hole = the rectangle it bores from the edge
/// (diameter x depth, the full dimension when through); groove = its rectangle (clipped to
/// the box [0,L]x[0,W], so a groove may run out through a straight box side); pocket = its
/// polygon. Tolerance 1e-3 mm (touching the outline is allowed).
core::Diagnostics checkOperationsInsideOutline(const domain::Part& part);

} // namespace fos::furniture
