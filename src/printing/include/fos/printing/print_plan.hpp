// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/core/diagnostic.hpp"
#include "fos/core/result.hpp"
#include "fos/drawings/sheet.hpp"

#include <string>
#include <vector>

/// Print planning: how a drawing sheet is mapped onto the pages of a physical printer.
///
/// planPrint() is a PURE function (no Qt, no printer needed). Everything it decides is
/// reported in the returned PrintPlan: the scale factor, the page transforms, the
/// tiling and the warnings. Nothing is ever scaled silently.
///
/// PAPER COORDINATES used here: millimetres, origin at the TOP-LEFT corner of the
/// physical paper as oriented, +X right, +Y DOWN (the natural frame of a printer page;
/// note that drawings::Sheet uses origin bottom-left, Y up).
namespace fos::printing {

/// Axis-aligned rectangle in paper coordinates (mm, top-left origin, Y down).
struct RectMm {
    double x = 0, y = 0, width = 0, height = 0;
    double right() const noexcept { return x + width; }
    double bottom() const noexcept { return y + height; }
};

/// What a printer offers for one page.
struct PrinterPageInfo {
    double paperWidth = 210;  ///< mm, as oriented (landscape: width > height).
    double paperHeight = 297; ///< mm, as oriented.
    /// Area the device can put ink on, in paper coordinates of the oriented page.
    RectMm printable{0, 0, 210, 297};
    /// Orientation that `paperWidth`/`paperHeight`/`printable` describe.
    drawings::PaperOrientation orientation = drawings::PaperOrientation::Portrait;

    /// Convenience: paper `width` x `height` mm with the same unprintable margin on all sides.
    static PrinterPageInfo withUniformMargin(double width, double height, double margin);
};

/// The same paper turned by 90 degrees. The unprintable margins are turned with the
/// paper (portrait top edge becomes the landscape left edge and back); a real driver
/// may report different margins, which is why printSheets() re-queries the printer.
PrinterPageInfo rotated(const PrinterPageInfo& page);

enum class PrintMode {
    /// 1 sheet mm = 1 paper mm. Fails when the drawn content does not fit the printable
    /// area, unless PrintPlanOptions::allowTiling is set (then several pages are planned).
    ExactScale,
    /// The whole sheet is scaled uniformly into the printable area. The factor is
    /// reported in PrintPlan::scaleFactor and stamped onto the page as visible text.
    FitToPage
};

struct PrintPlanOptions {
    /// ExactScale only: split a sheet that does not fit into several overlapping pages.
    bool allowTiling = false;
    /// Width (mm) of the strip that neighbouring tiles both show. Must be >= 0 and
    /// smaller than the tile content size.
    double tileOverlap = 10.0;
    /// Margin (mm) kept free inside the printable area around a tile; crop marks,
    /// registration marks and the page label are drawn there. Must be >= 0.
    double tileMargin = 5.0;
    /// Text height (mm) of tile labels and of the fit-to-page stamp.
    double labelHeight = 3.0;
    /// Tile label. Placeholders: {row} {col} (1-based, row 1 = top of the sheet,
    /// col 1 = left), {rows} {cols}. Localisable.
    std::string tileLabel = "{row}/{col}";
    /// May the planner use the paper turned by 90 degrees when that is better (sheet
    /// fits / larger factor / fewer tiles)? The orientation chosen is in the plan.
    bool allowRotatePaper = true;
    /// FitToPage only: enlarge a sheet smaller than the printable area (factor > 1).
    /// Off by default: a sheet that already fits is printed at exactly 1:1.
    bool allowEnlarge = false;
    /// FitToPage only: height (mm) of a strip reserved at the bottom of the printable
    /// area for the stamp, so it never overlaps the drawing. 0 = no strip; the stamp is
    /// then drawn over the bottom-left corner of the printable area.
    double stampStrip = 6.0;
    /// FitToPage stamp, printed whenever the factor differs from 1. {percent} is
    /// replaced by the factor in percent with one decimal ("70.7"). Localisable (UTF-8).
    std::string scaledLabel = "Scaled to {percent} % \xE2\x80\x94 not to scale";
};

/// Straight mark (crop or registration mark) in paper coordinates, drawn 0.2 mm wide.
struct PlanMark {
    double x1 = 0, y1 = 0, x2 = 0, y2 = 0;
};

/// Single-line text in paper coordinates; (x, y) is the left end of the baseline.
struct PlanText {
    double x = 0, y = 0;
    double height = 3.0; ///< Font em size, mm.
    std::string text;    ///< UTF-8.
};

/// One physical page.
struct PrintPage {
    int row = 1, col = 1; ///< 1-based tile position (1/1 for an untiled plan).
    /// Mapping transform sheet -> paper: for a sheet point (sx, sy) (sheet mm, origin
    /// bottom-left, Y up):
    ///   paperX = offsetX + scale * sx
    ///   paperY = offsetY + scale * (sheetHeight - sy)
    double scale = 1.0;
    double offsetX = 0, offsetY = 0;
    /// Paper area the sheet may be painted in (painting is clipped to it).
    RectMm clip;
    /// Part of the sheet shown on this page, in sheet mm measured from the sheet's
    /// TOP-left corner, Y down (for a tile: its content window).
    RectMm sheetRegion;
    std::vector<PlanMark> marks; ///< Crop marks (tile corners) and registration marks (join lines).
    std::vector<PlanText> texts; ///< Tile label and/or scale stamp.
    std::string label;           ///< Tile label text ("2/3"), empty when not tiled.
};

struct PrintPlan {
    PrintMode mode = PrintMode::ExactScale;
    /// Effective scale factor paper/sheet. Exactly 1.0 for ExactScale; e.g. 0.7071 for
    /// an A3 sheet fitted onto A4. NOTE: the drawing's own scale (Sheet::scale, e.g.
    /// 1:10) is separate; on paper they multiply.
    double scaleFactor = 1.0;
    bool tiled = false;
    int rows = 1, cols = 1;
    PrinterPageInfo page; ///< Page geometry actually planned for (possibly rotated).
    bool paperRotated = false; ///< True when `page` is the caller's page turned by 90 degrees.
    std::vector<PrintPage> pages; ///< Row by row from the top-left tile.
    bool calibrationMarkPresent = false;
    /// Length the sheet's 100 mm calibration mark will have on paper (100 x scaleFactor);
    /// 0 when the sheet has no calibration mark.
    double calibrationPrintedLength = 0.0;
    /// Always contains "printing.driver_scaling" and one of
    /// "printing.calibration.present" / "printing.calibration.absent"; see the README
    /// for the full list of codes.
    core::Diagnostics warnings;
};

/// Plans printing `sheet` on pages described by `page`.
///
/// ExactScale: the sheet is centred on the paper; if drawn content would fall outside
/// the printable area the sheet is shifted by the smallest amount that brings all
/// content inside (reported as "printing.shifted"). "Content" is the bounding box of
/// the sheet's primitives (drawings::boundsOf, line weights included; text extents are
/// the drawings module's estimate). If the content is larger than the printable area:
/// ConstraintViolation naming both sizes, or a tiling when `allowTiling` is set.
///
/// Tiling: tile content size c = printable size - 2 x tileMargin, step = c - tileOverlap,
/// count per axis = 1 if sheetSize <= c else ceil((sheetSize - overlap) / step).
/// The whole sheet (not only its content) is tiled, starting at its top-left corner.
///
/// FitToPage: factor = min(printableW / sheetW, (printableH - stampStrip) / sheetH),
/// limited to 1 unless `allowEnlarge`; the sheet is centred in the remaining area.
///
/// Fails with InvalidArgument for non-positive sizes, a printable area outside the
/// paper, or tiling parameters that leave no room.
core::Result<PrintPlan> planPrint(const drawings::Sheet& sheet, const PrinterPageInfo& page, PrintMode mode,
                                  const PrintPlanOptions& options = {});

/// Replaces {percent} in `labelTemplate` by `scaleFactor` in percent with one decimal:
/// formatScaleStamp("Scaled to {percent} %", 0.70707) == "Scaled to 70.7 %".
std::string formatScaleStamp(const std::string& labelTemplate, double scaleFactor);

} // namespace fos::printing
