// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/nesting/types.hpp"

#include <string>

namespace fos::nesting {

/// Renders one sheet layout as a standalone SVG document (UTF-8, text XML-escaped).
/// 1 user unit = 1 mm: viewBox = "0 0 <sheet length> <sheet width>", width/height in mm.
/// The sheet origin is drawn at the BOTTOM-LEFT (svg y = sheet width - y).
/// CSS classes: fos-sheet, fos-margin, fos-area (placement area), fos-part, fos-label,
/// fos-remnant. Unknown sheet ids render the sheet with the layout's used length only.
std::string renderNestSvg(const Job& job, const SheetLayout& layout);

/// Escapes & < > " ' for use in XML text and attribute values.
std::string xmlEscape(const std::string& text);

} // namespace fos::nesting
