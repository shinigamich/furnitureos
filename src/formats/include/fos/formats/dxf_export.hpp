// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// DXF export of part outlines for CNC / cutting software (write-only). Format, layers and
/// limits: src/formats/README.md, "DXF part outline export".
///
/// FORMAT: ASCII DXF R12 (AC1009): HEADER ($ACADVER AC1009, $INSUNITS 4 = mm, $EXTMIN/$EXTMAX),
/// TABLES (LTYPE CONTINUOUS, one LAYER entry per used layer), an empty BLOCKS section and
/// ENTITIES; no handles, no blocks, no INSERTs. Closed outlines are 2D POLYLINEs (flag 70 = 1)
/// whose arcs are exact VERTEX bulges (group 42); holes are CIRCLEs; the part id is a TEXT.
/// R12 is the most widely read DXF dialect (AutoCAD, LibreCAD, QCAD, DraftSight, CNC/nesting
/// software); $INSUNITS is an R2000 variable that R12 readers ignore - units are mm regardless.
///
/// UNITS: mm. FRAME: each part is drawn in its PART FRAME seen from face A (+Z towards the
/// viewer, u = x, v = y; face B is not mirrored, ARCHITECTURE.md). In a single file the parts
/// are placed in a row along +X: each part shifted by (cursor - minX, 0) (rounded to 1e-6 mm) of its finished-outline
/// bounding box, cursor advancing by that box's length plus `spacing`. One file per part keeps
/// the part frame unshifted.
#include "fos/core/diagnostic.hpp"
#include "fos/core/result.hpp"
#include "fos/domain/model.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace fos::formats {

struct DxfExportOptions {
    /// false (default): OUTLINE is the raw outline domain::cutContour() (before edge banding:
    /// what the saw/router cuts). true: the finished outline domain::finishedContour().
    bool finishedOutline = false;
    /// true: one file per part (part frame unshifted); false: all parts in one file in a row.
    bool oneFilePerPart = false;
    /// Only the part with this id (empty = every part of every assembly, model order).
    std::string partId;
    double spacing = 50.0;    ///< mm between neighbouring parts in a single file; >= 0.
    double textHeight = 10.0; ///< mm, height of the part-id TEXT (layer LABELS); > 0.
};

struct DxfExportFile {
    std::string partId;   ///< One-file-per-part: the part; single file: empty.
    std::string fileName; ///< One-file-per-part: "<id with every char outside [A-Za-z0-9._-] as '_'>.dxf"
                          ///< made unique with "-2", "-3"...; single file: empty (caller names it).
    std::string text;     ///< DXF text ('\n' line ends, ASCII; non-ASCII text as \U+XXXX).
};

struct DxfExport {
    std::vector<DxfExportFile> files; ///< One file, or one per exported part (model order).
    std::size_t partCount = 0;        ///< Parts exported.
    /// Warnings "formats.dxf_export.operations_skipped" naming each part whose operations
    /// cannot be shown seen from face A (blind holes/pockets on B, grooves on B, anything on
    /// an edge face), and "formats.dxf_export.invalid_operation" for degenerate ones.
    core::Diagnostics diagnostics;
};

/// Layers (only those used are declared):
///   OUTLINE   the outer contour (raw by default, see finishedOutline), closed POLYLINE.
///   CUTOUTS   PartShape cut-outs and through pockets (face A or B), closed POLYLINEs.
///   POCKETS   blind pockets on face A, closed POLYLINEs (depth is not encoded).
///   DRILL_<d> holes on face A and through holes on face B, CIRCLE of diameter d; d written
///             in shortest form with '.' as '_' (5 -> DRILL_5, 8.5 -> DRILL_8_5).
///   GROOVES   grooves on face A as their closed rectangular footprint (centre line +- width/2).
///   LABELS    the part id as TEXT above the part.
/// Errors: no part in the model, or none with id `partId` (NotFound); invalid options
/// (InvalidArgument); a part whose raw outline cannot be computed or whose outline is not a
/// valid contour (InvalidArgument naming the part).
core::Result<DxfExport> exportPartOutlinesDxf(const domain::Model& model, const DxfExportOptions& options = {});

} // namespace fos::formats
