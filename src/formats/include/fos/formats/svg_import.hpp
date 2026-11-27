// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Import of 2D outlines from SVG files (a safe, minimal subset; no external dependency). The
/// loops it returns feed formats::importPanelShape() / project::buildOutlinePanelItem() exactly
/// like the DXF import. Exact rules and limits: src/formats/README.md, "SVG outline import".
///
/// Supported: <path d> with M L H V A Z (absolute and relative; arcs only when circular after
/// all transforms), <rect> (rx / ry corners), <circle>, <ellipse> (only when circular), <line>,
/// <polyline>, <polygon>; `transform` on any element with translate / scale / rotate / matrix.
/// Refused (Unsupported, whole import): Bezier commands C S Q T, elliptical arcs / ellipses /
/// rounded corners that are not circular after the transform, skewX / skewY.
/// Not imported (warning): <text>, <image>, <use>, <foreignObject>, nested <svg>; content of
/// <defs>, <symbol>, <clipPath>, <mask>, <pattern>, <marker> is never drawn; display:none
/// subtrees are skipped (info). Nothing is ever fetched (no external DTD, image or reference).
///
/// UNITS: output in mm. One user unit is (in this order): options.scale mm; else derived from the
/// root width / height (mm, cm, in, pt, pc, px = 1/96 in, unitless = px) over the viewBox
/// (preserveAspectRatio "none" allows different x / y scales, otherwise the uniform meet/slice
/// scale); else (no viewBox, or no width/height) 1 px = 25.4 / 96 mm.
/// FRAME: SVG's y axis points down; the output is mirrored (y -> -y) so the outline appears as
/// drawn (top of the drawing = +y). Untrusted input: size / element / depth caps; malformed
/// input is a ParseError naming the 1-based line; nothing throws.
#include "fos/core/diagnostic.hpp"
#include "fos/core/result.hpp"
#include "fos/geometry/contour.hpp"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fos::formats {

struct SvgImportOptions {
    /// Millimetres per user unit (> 0, <= 1e6); overrides width / height / viewBox units.
    std::optional<double> scale;
    /// Endpoint matching distance (mm) for open pieces (<line>, <polyline>, unclosed subpaths)
    /// chained into loops. Must be in (0, 10].
    double chainTolerance = 0.01;
    std::size_t maxBytes = std::size_t{16} * 1024 * 1024; ///< Largest accepted text.
    std::size_t maxElements = 100000;                     ///< XML elements in the file.
    std::size_t maxDepth = 64;                            ///< Element nesting depth.
    std::size_t maxPieces = 500000;                       ///< Segments over all shapes.
    std::size_t maxLoopVertices = 10000;                  ///< Vertices of one output loop.
};

/// One closed loop found in the file.
struct SvgLoop {
    geometry::Contour contour; ///< mm, counter-clockwise (|bulge| <= 1).
    std::size_t line = 0;      ///< 1-based line of the (first) element it was built from.
    std::string element;       ///< That element, e.g. "<path id=\"outer\">" or "<rect>".
};

struct SvgImport {
    std::vector<SvgLoop> loops;    ///< Ordered by line.
    core::Diagnostics diagnostics; ///< Codes "formats.svg.*"; warnings never silently drop geometry.
    double mmPerUnitX = 1, mmPerUnitY = 1; ///< Scale applied to user units.
    std::string unitSource;        ///< How the scale was found (for messages), e.g. "width 210mm over viewBox width 210".
    std::size_t shapeCount = 0;    ///< Shape elements converted.
    std::size_t elementCount = 0;  ///< XML elements read.
};

/// Parses SVG text. Errors: ParseError (malformed XML / numbers / attributes), Unsupported (see
/// file comment; DOCTYPE internal subsets, non-predefined entities, UTF-16), InvalidArgument
/// (caps, coordinates beyond 1e8 mm, root not <svg>, unusable width/height/viewBox, options).
core::Result<SvgImport> importSvg(std::string_view text, const SvgImportOptions& options = {});

/// Reads `path` (regular file, at most options.maxBytes) and calls importSvg.
core::Result<SvgImport> importSvgFile(const std::filesystem::path& path, const SvgImportOptions& options = {});

/// The contours of `import.loops`, in order (input of importPanelShape / buildOutlinePanelItem).
std::vector<geometry::Contour> contoursOf(const SvgImport& import);

} // namespace fos::formats
