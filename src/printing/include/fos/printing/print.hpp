// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/core/result.hpp"
#include "fos/printing/paint.hpp"
#include "fos/printing/print_plan.hpp"

#include <QImage>

#include <vector>

class QPainter;
class QPrinter;

/// Execution of print plans on a QPrinter (physical printer or "print to PDF").
namespace fos::printing {

struct PrintOptions {
    PrintMode mode = PrintMode::ExactScale;
    PrintPlanOptions plan;
    PaintOptions paint; ///< `paint.unitsPerMm` is ignored (derived from the printer resolution).
    /// Try to switch the printer to a paper of exactly the sheet's size and orientation
    /// before planning. When the printer refuses (size not supported), the paper it was
    /// configured with is kept and the plan is made for that paper.
    bool matchPaperToSheet = true;
};

/// Page geometry of the printer as currently configured: oriented paper size and the
/// printable area (paper minus the device's minimum margins), in mm.
PrinterPageInfo printerPageInfo(const QPrinter& printer);

/// Paints one page of a plan. The painter's origin must be the TOP-LEFT CORNER OF THE
/// PAPER (QPrinter in full-page mode); units are converted to mm per
/// PaintOptions::unitsPerMm. Draws the sheet through the page's transform, clipped to
/// PrintPage::clip, then the marks (0.2 mm lines) and texts of the page.
void paintPlanPage(QPainter& painter, const drawings::Sheet& sheet, const PrintPage& page,
                   const PaintOptions& options = {});

/// Raster image of one planned page (whole paper, white) for a print preview; pixel
/// (0, 0) is the paper's top-left corner. Null image for an invalid page index,
/// resolution or size (same limits as renderPreview()).
QImage renderPlanPagePreview(const drawings::Sheet& sheet, const PrintPlan& plan, std::size_t pageIndex,
                             double pixelsPerMm, const PaintOptions& options = {});

/// Prints all sheets. The printer is switched to full-page mode; for every sheet the
/// paper size/orientation is set when the printer supports it (see
/// PrintOptions::matchPaperToSheet), the printer's real page geometry is read back and
/// planPrint() is run against it. ALL sheets are planned before anything is printed:
/// if one plan fails, its error is returned and nothing is sent to the printer.
/// `executedPlans` (optional) receives one plan per sheet, including the warnings the
/// user interface must show (scale factor, driver scaling, calibration mark).
///
/// Errors: those of planPrint(); InvalidArgument (no sheets); Internal (no
/// QGuiApplication); IoError (printer could not start, a page failed, printer error).
/// The application cannot detect scaling applied by the printer driver.
core::Result<void> printSheets(QPrinter& printer, const std::vector<drawings::Sheet>& sheets,
                               const PrintOptions& options = {}, std::vector<PrintPlan>* executedPlans = nullptr);

} // namespace fos::printing
