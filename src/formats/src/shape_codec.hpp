// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
// Private: JSON codec and semantic checks of domain::PartShape (ADR-0008). Not installed.
#include "fos/core/result.hpp"
#include "fos/domain/part.hpp"
#include "fos/formats/json.hpp"

#include <string_view>

namespace fos::formats::detail {

/// {"outer": [...], "cutouts": [[...], ...], "outerEdges": [null | "<band id>", ...]};
/// a vertex is {"x", "y"} plus "bulge" when it is not 0.
Json toJson(const domain::PartShape& shape);

/// Structural decode of a "shape" object at `path` (types, unknown keys, the vertex/cut-out
/// limits of codec.hpp). Geometry is checked by checkPartShape().
core::Result<domain::PartShape> shapeFromJson(const Json& json, std::string_view path);

/// Semantic checks of `part.shape` (no-op without a shape), in this order:
///   Part::edges empty; limits; outer contour valid (geometry::validateContour), counter-
///   clockwise, bounding box [0,L]x[0,W] within kShapeBoundsTolerance; each cut-out valid,
///   every vertex strictly inside the outer contour and no contact with any outer segment;
///   cut-outs pairwise without contact and not nested; outerEdges empty or one non-empty
///   id per outer segment.
/// Errors are ParseError "<partPath>/shape/...: <message>" (JSON-pointer locations, context =
/// location), so the codec can return them unchanged.
core::Result<void> checkPartShape(const domain::Part& part, std::string_view partPath);

} // namespace fos::formats::detail
