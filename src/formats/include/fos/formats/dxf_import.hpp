// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Import of 2D outlines from ASCII DXF (R12 .. current; the publicly documented group-code
/// format) and their conversion into a panel outline with cut-outs. Exact rules, supported
/// entities and limits: src/formats/README.md, "DXF outline import".
///
/// UNITS: every output coordinate is millimetres. FRAME: the DXF world XY plane (Z ignored);
/// importPanelShape() moves the result into the part frame (bounding box starting at (0,0)).
/// DXF files are untrusted input: size and entity caps apply, malformed input is a ParseError
/// naming the 1-based line, nothing throws.
#include "fos/core/diagnostic.hpp"
#include "fos/core/result.hpp"
#include "fos/domain/part.hpp"
#include "fos/geometry/contour.hpp"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fos::formats {

/// Drawing unit of a DXF file.
enum class DxfUnit { Millimetre, Centimetre, Metre, Inch, Foot };

/// "mm", "cm", "m", "in", "ft".
const char* toString(DxfUnit unit) noexcept;
/// Inverse of toString (also accepts "inch"); nullopt for anything else.
std::optional<DxfUnit> dxfUnitFromString(std::string_view text) noexcept;
/// Millimetres per drawing unit (in = 25.4, ft = 304.8, ...).
double millimetresPer(DxfUnit unit) noexcept;

struct DxfImportOptions {
    /// Overrides the file's $INSUNITS. When unset, $INSUNITS decides (1 in, 2 ft, 4 mm, 5 cm,
    /// 6 m); absent or 0 (unitless) means mm with a warning; any other code is an error.
    std::optional<DxfUnit> units;
    /// Only entities on this layer (DXF layer names compare case-insensitively). Empty = all.
    std::string layer;
    /// Endpoint matching distance (mm, after unit scaling) when LINE / ARC / open polyline
    /// pieces are chained into loops; also closes a polyline whose last vertex repeats the
    /// first. Must be in (0, 10].
    double chainTolerance = 0.01;
    std::size_t maxBytes = std::size_t{32} * 1024 * 1024; ///< Largest accepted text.
    std::size_t maxEntities = 100000;                     ///< Entities in the ENTITIES section.
    std::size_t maxLoopVertices = 10000;                  ///< Vertices of one output loop.
};

/// One closed loop found in the file.
struct DxfLoop {
    geometry::Contour contour; ///< mm, counter-clockwise (geometry bulge convention, |bulge| <= 1).
    std::size_t line = 0;      ///< 1-based line of the (first) entity it was built from.
    std::string layer;         ///< Layer of that entity.
};

struct DxfImport {
    std::vector<DxfLoop> loops;    ///< File order of their first entity.
    core::Diagnostics diagnostics; ///< Codes "formats.dxf.*"; warnings never silently drop geometry.
    DxfUnit unit = DxfUnit::Millimetre; ///< Unit applied.
    std::size_t entityCount = 0;   ///< Entities read in the ENTITIES section (all layers).
};

/// Parses ASCII DXF text. Supported: LINE, ARC, CIRCLE, LWPOLYLINE (bulges, closed flag),
/// 2D POLYLINE/VERTEX/SEQEND (bulges); entities with extrusion (0,0,-1) are mirrored back into
/// world XY. Loose pieces are chained into loops by endpoint matching; open chains, INSERT
/// (blocks are not supported), other entity types, other extrusions and loops that are not
/// simple contours are reported as diagnostics. Errors (ParseError / Unsupported /
/// InvalidArgument): binary DXF, size or entity caps, a group code that is not an integer, a
/// number that does not parse or is not finite, text ending inside a pair or section
/// (truncated), no ENTITIES section, unknown $INSUNITS, coordinates beyond 1e8 mm, invalid options.
core::Result<DxfImport> importDxf(std::string_view text, const DxfImportOptions& options = {});

/// Reads `path` (regular file, at most options.maxBytes) and calls importDxf.
core::Result<DxfImport> importDxfFile(const std::filesystem::path& path, const DxfImportOptions& options = {});

/// A cut-out recognised as a primitive shape (part frame, mm).
struct CutoutPrimitive {
    enum class Kind { Circle, Rectangle, Other };
    Kind kind = Kind::Other;
    double x = 0, y = 0;        ///< Circle: centre. Rectangle: minimum corner.
    double length = 0, width = 0; ///< Rectangle: size along x / y.
    double radius = 0;          ///< Circle radius, or the (equal) corner radius of a Rectangle (0 = sharp).
};

/// Result of importPanelShape(): the outline in the part frame.
struct PanelShapeImport {
    double length = 0; ///< Bounding-box size along x (mm).
    double width = 0;  ///< Bounding-box size along y (mm).
    /// outer: the outline, CCW, bounding box [0,length] x [0,width]; cutouts: the loops inside
    /// it (CCW), sorted by their minimum corner (y, then x); outerEdges empty (no bands).
    domain::PartShape shape;
    std::vector<CutoutPrimitive> cutoutKinds; ///< Parallel to shape.cutouts.
    /// Loops that are not used (outside the outline, crossing it, crossing or inside another
    /// cut-out): warnings "formats.dxf.loop_ignored" naming each loop's position.
    core::Diagnostics diagnostics;
};

/// The largest-area loop is the outline; loops strictly inside it (not touching it, not inside
/// or crossing another cut-out) are cut-outs; everything is translated so the outline's
/// bounding box starts at (0,0); coordinates within 1e-9 mm of a 1e-6 mm grid are snapped to
/// it (removes unit-conversion noise such as 2.5400000000000001). Errors: no loop
/// (InvalidArgument). Primitive recognition: all-arc loops on one circle -> Circle; 4 straight
/// axis-parallel sides -> Rectangle; 8 segments alternating axis-parallel sides and 90-degree
/// arcs of one radius -> Rectangle with corner radius; anything else -> Other.
core::Result<PanelShapeImport> importPanelShape(const std::vector<geometry::Contour>& loops);

} // namespace fos::formats
