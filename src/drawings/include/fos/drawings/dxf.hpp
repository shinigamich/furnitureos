// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/drawings/render.hpp"

#include <string>
#include <string_view>
#include <vector>

/// ASCII DXF R12 (AC1009) export of MODEL-SPACE geometry at 1:1 in millimetres.
///
/// Structure: HEADER, TABLES (LTYPE, LAYER, STYLE), BLOCKS (empty), ENTITIES, EOF.
/// Entities used: LINE, CIRCLE, ARC, TEXT only.
/// Layers: VISIBLE (visible and border lines), HIDDEN (dashed), CENTER, DIMENSION
/// (dimension/extension lines, leaders, terminators), HATCH (thin lines such as
/// section hatching), TEXT (all texts). Layer "0" is defined but unused.
///
/// Limitations (inherent to R12 or deliberate):
///  * $INSUNITS (introduced in AC1015) and $MEASUREMENT (AC1014) do not exist in R12,
///    so a strictly valid R12 file cannot state its unit. By default they are NOT
///    written; set DxfOptions::writeUnitVariables to add `$INSUNITS = 4` (mm) and
///    `$MEASUREMENT = 1` (metric), which many importers honour although the file is
///    then no longer strict R12. Either way the numbers are millimetres.
///  * Polylines are written as individual LINE entities; arrowheads as three LINEs
///    (outline, not filled); dots as CIRCLEs.
///  * Line weights are not representable in R12 and are dropped.
///  * Text is ASCII only: other characters are written as `\U+XXXX` (understood by
///    AutoCAD and most importers); code points above U+FFFF and control characters
///    become '?'. Font metrics are those of the importing program.
///  * No associative DIMENSION entities: dimensions arrive as lines and text.
namespace fos::drawings {

struct DxfOptions {
    bool writeUnitVariables = false;
    /// $LTSCALE: multiplier of the dash patterns (defined in paper mm). Use the same
    /// factor as the annotation scale of the exported geometry.
    double linetypeScale = 1.0;
};

/// Entity counts of a DXF produced by toDxf() for the same primitives.
struct DxfEntityCounts {
    std::size_t lines = 0, circles = 0, arcs = 0, texts = 0;
};
DxfEntityCounts countDxfEntities(const std::vector<Primitive>& primitives);

/// Writes primitives whose coordinates are interpreted as model millimetres (Y up).
std::string toDxf(const std::vector<Primitive>& primitives, const DxfOptions& options = {});

/// View geometry and annotations at 1:1 (placement origin (0,0), scale 1) with all
/// annotation sizes multiplied by `annotationScale` so they stay legible next to
/// full-size furniture (e.g. 10 -> 25 mm high dimension text).
std::vector<Primitive> modelSpacePrimitives(const View& view, const ViewAnnotations& annotations,
                                            const DrawingStyle& style = {}, double annotationScale = 10.0);

/// Convenience: toDxf(modelSpacePrimitives(...)) with $LTSCALE = annotationScale.
std::string viewToDxf(const View& view, const ViewAnnotations& annotations, const DrawingStyle& style = {},
                      double annotationScale = 10.0, DxfOptions options = {});

/// ASCII-only DXF text: non-ASCII code points as \U+XXXX (see limitations).
std::string dxfEscapeText(std::string_view utf8);

} // namespace fos::drawings
