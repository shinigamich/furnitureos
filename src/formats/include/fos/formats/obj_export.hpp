// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/core/diagnostic.hpp"
#include "fos/core/result.hpp"
#include "fos/domain/model.hpp"

#include <filesystem>
#include <string>

/// Wavefront OBJ export (write-only) of a resolved model for viewing in other tools.
/// Scope and limits are documented in src/formats/README.md ("Wavefront OBJ").
namespace fos::formats {

/// Chord tolerance (mm) used to tessellate the arcs of shaped parts (ADR-0008): every
/// chord stays within this distance of its exact arc; contour vertices are kept exactly.
inline constexpr double kObjChordTolerance = 0.05;

struct ObjExportOptions {
    /// File name written into the `mtllib` statement. Must be non-empty and free of
    /// whitespace/control characters.
    std::string mtlFileName = "model.mtl";
};

struct ObjExport {
    std::string obj;               ///< Contents of the .obj file (UTF-8, '\n' line ends).
    std::string mtl;               ///< Contents of the companion .mtl file.
    core::Diagnostics diagnostics; ///< Warnings, e.g. a part referencing a missing material.
};

/// Exports every part of every assembly as a box [0,L]x[0,W]x[0,T] (finished size)
/// placed in the PROJECT frame (assembly placement * part placement).
///  * Units: millimetres, coordinates written with full double precision.
///  * Axes: FurnitureOS project frame, right-handed, Z up (no conversion to Y-up).
///  * One `o` + `g` per part, 8 vertices, 6 face normals, 6 quads with outward
///    counter-clockwise winding; `usemtl` names the part's material.
///  * MTL: one material per referenced material with its display colour as `Kd`.
///  * Shaped parts (`Part::shape`, ADR-0008) instead: the finished outer contour with its
///    cut-outs, arcs tessellated with kObjChordTolerance, extruded over [0, T]: 2n vertices
///    for n outline points, normals -Z, +Z and one per outline edge, triangulated caps
///    (holes included) and one quad per outline edge, all wound counter-clockwise seen
///    from outside. A header comment line about shaped parts is added only when the model
///    has one, so exports of rectangular models are unchanged.
/// Not exported: machining operations, edge bands, hardware, textures, grain.
/// Fails (InvalidArgument) for a part with a non-positive/non-finite size, a non-rigid
/// placement, a shape that fails the codec's shape checks, or an outline that cannot be
/// meshed; the message names the part.
core::Result<ObjExport> exportObj(const domain::Model& model, const ObjExportOptions& options = {});

/// exportObj + atomic write of `objPath` and of the MTL file beside it. The MTL file is
/// named after the OBJ file (`<stem>.mtl`, whitespace replaced by '_').
core::Result<ObjExport> writeObjFiles(const std::filesystem::path& objPath, const domain::Model& model);

} // namespace fos::formats
