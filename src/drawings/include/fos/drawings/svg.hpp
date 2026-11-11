// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/drawings/sheet.hpp"

#include <string>
#include <string_view>

/// SVG 1.1 export of a sheet at exact scale.
///
/// The root element is `<svg width="<w>mm" height="<h>mm" viewBox="0 0 <w> <h>">`,
/// so ONE USER UNIT IS EXACTLY ONE MILLIMETRE. Paper Y (up) is flipped into SVG Y
/// (down) per coordinate: y_svg = h - y_paper (no mirroring transform, so text stays
/// readable). Stroke widths and dash patterns are in mm. Numbers are written with up
/// to 4 decimals (0.1 micrometre). Text is XML-escaped UTF-8; rotation is written as
/// `rotate(-angle x y)`; vertical anchors are realised by shifting the baseline
/// (Middle 0.35 x height, Top 0.7 x height) rather than `dominant-baseline`, which
/// renderers support unevenly. `sourceId` / `elementId` are emitted as `data-source` /
/// `data-element` attributes.
namespace fos::drawings {

struct SvgOptions {
    std::string fontFamily = "Arial, Helvetica, sans-serif";
    std::string strokeColor = "#000000";
    std::string background; ///< Empty = transparent; otherwise a fill colour for the page.
    bool dataAttributes = true;
};

std::string toSvg(const Sheet& sheet, const SvgOptions& options = {});

/// Escapes & < > " ' and replaces control characters that XML 1.0 forbids with spaces.
std::string xmlEscape(std::string_view utf8);

} // namespace fos::drawings
