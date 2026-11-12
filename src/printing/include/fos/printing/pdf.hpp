// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/core/result.hpp"
#include "fos/drawings/sheet.hpp"
#include "fos/printing/paint.hpp"

#include <filesystem>
#include <string>
#include <vector>

/// Vector PDF export at exact scale (QPdfWriter).
namespace fos::printing {

struct PdfOptions {
    std::string title;                   ///< Document title metadata (UTF-8). Empty = id of the first sheet.
    std::string creator = "FurnitureOS"; ///< Creator metadata (UTF-8).
    /// Resolution of the PDF coordinate grid in dots per inch (72 .. 9600). Output is
    /// vector; this only sets the precision of coordinates (1200 dpi = 0.021 mm).
    int resolutionDpi = 1200;
    PaintOptions paint; ///< `paint.unitsPerMm` is ignored (always derived from the resolution).
};

/// Writes one PDF page per sheet. Each page has exactly the sheet's size (custom page
/// size in millimetres, zero margins, full-page mode) and the sheet is painted 1:1, so
/// 1 sheet mm = 1 mm on the page = 72/25.4 pt. Lines are vector paths and text is real
/// text with embedded fonts. (Qt writes the /MediaBox in whole points, so the page box
/// can differ from the sheet by up to 0.5 pt = 0.18 mm; the content scale is unaffected
/// and the sheet's top-left corner is the page's top-left corner.)
///
/// Fails with InvalidArgument (no sheets, invalid sheet size, resolution out of range),
/// Internal (no QGuiApplication) or IoError (file cannot be written). Requires a
/// QGuiApplication. An existing file is overwritten.
core::Result<void> exportPdf(const std::vector<drawings::Sheet>& sheets, const std::filesystem::path& file,
                             const PdfOptions& options = {});

} // namespace fos::printing
