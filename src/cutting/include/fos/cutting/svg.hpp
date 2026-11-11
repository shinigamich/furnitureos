// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/cutting/types.hpp"

#include <string>

namespace fos::cutting {

struct SvgOptions {
    bool legend = true;      ///< Adds a title line and a legend strip below the sheet.
    bool pieceLabels = true; ///< Label and size text inside each piece.
    bool offcutSizes = true; ///< Size text inside reusable remnants.
};

/// Renders one sheet as a standalone SVG document (UTF-8, all text XML-escaped).
/// 1 user unit = 1 mm: viewBox = "0 0 <sheet length> <sheet width + legend height>" and
/// width/height carry the same numbers in mm. The sheet origin is drawn at the BOTTOM-LEFT
/// (sheet +Y points up on the drawing, i.e. svg y = sheet width - y).
/// CSS classes: fos-trim, fos-usable, fos-piece, fos-kerf, fos-remnant, fos-waste, fos-label.
std::string renderSheetSvg(const Job& job, const SheetLayout& sheet, const SvgOptions& options = {});

/// Escapes & < > " ' for use in XML text and attribute values.
std::string xmlEscape(const std::string& text);

} // namespace fos::cutting
