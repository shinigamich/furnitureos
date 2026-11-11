// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/core/result.hpp"
#include "fos/drawings/primitives.hpp"
#include "fos/drawings/view.hpp"

#include <string>
#include <vector>

/// Sheets: paper, frame, title block, calibration mark, drawing scale.
/// All coordinates are PAPER space (mm, origin bottom-left, Y up).
namespace fos::drawings {

enum class PaperFormat { A4, A3, A2, A1, Custom };
enum class PaperOrientation { Portrait, Landscape };

struct PaperSize {
    double width = 0, height = 0; ///< mm, as oriented.
};

/// ISO 216 sizes: A4 210x297, A3 297x420, A2 420x594, A1 594x841 (portrait).
/// For Custom, `custom` is given in portrait or landscape freely and is re-oriented:
/// Portrait -> width <= height, Landscape -> width >= height.
/// Fails (InvalidArgument) for a custom size that is not positive.
core::Result<PaperSize> paperSize(PaperFormat format, PaperOrientation orientation, PaperSize custom = {});
const char* toString(PaperFormat f) noexcept;

struct Margins {
    double left = 20, right = 10, top = 10, bottom = 10; ///< mm (filing margin on the left).
};

/// Drawing scale numerator:denominator (paper : model). 1:10 -> factor 0.1.
struct Scale {
    double numerator = 1, denominator = 1;
    double factor() const noexcept { return numerator / denominator; }
    /// "1:10", "2:1", "1:7.5" (core::formatLength formatting, 3 decimals).
    std::string text() const;
};

/// Standard reduction scales tried by fit-to-page, largest first:
/// 1:1, 1:2, 1:5, 1:10, 1:20, 1:50, 1:100.
const std::vector<Scale>& standardScales();

struct ScaleRequest {
    enum class Mode {
        FitToPage, ///< Largest scale of standardScales() at which everything fits.
        Explicit   ///< Exactly `scale`; generation fails when the views do not fit.
    };
    Mode mode = Mode::FitToPage;
    Scale scale{}; ///< Used for Mode::Explicit (any positive ratio; custom scales allowed).

    static ScaleRequest fit() { return {}; }
    static ScaleRequest explicitScale(double numerator, double denominator) {
        return {Mode::Explicit, {numerator, denominator}};
    }
};

/// Captions of the title block. Defaults are English; replace for localisation.
struct TitleBlockLabels {
    std::string project = "Project";
    std::string title = "Title";
    std::string author = "Author";
    std::string date = "Date";
    std::string scale = "Scale";
    std::string sheet = "Sheet";
};

struct TitleBlockData {
    std::string project;
    std::string title;
    std::string author;
    std::string date; ///< Supplied by the caller; this module never reads the clock.
    int sheetNumber = 1;
    int sheetCount = 1;
};

enum class CalibrationKind {
    None,
    Line100,       ///< 100 mm horizontal line with ticks every 10 mm.
    Rectangle10050 ///< 100 mm x 50 mm rectangle.
};

struct CalibrationOptions {
    CalibrationKind kind = CalibrationKind::Line100;
    std::string unit = "mm"; ///< Unit suffix of the generated size label ("100 mm" / "100 x 50 mm").
    /// Note printed under the mark (one Text per entry). Default English.
    std::vector<std::string> noteLines = {
        "Print at 100 % (\"actual size\"), without \"fit to page\".",
        "Check with a ruler: the mark above must measure exactly the size stated."};
};

struct SheetOptions {
    PaperFormat format = PaperFormat::A3;
    PaperOrientation orientation = PaperOrientation::Landscape;
    PaperSize custom{};
    Margins margins;
    TitleBlockData title;
    TitleBlockLabels labels;
    CalibrationOptions calibration;
    ScaleRequest scale;
};

/// A view as placed on a sheet; lets a UI convert between paper and view coordinates.
struct PlacedView {
    std::string viewId;
    ViewPlacement placement;
    Rect2 paperBounds; ///< Paper box of the view including its annotations.
};

struct Sheet {
    std::string id;
    double width = 0, height = 0; ///< Paper size, mm.
    Scale scale;                  ///< Scale of the views (also printed in the title block).
    Rect2 frame;                  ///< Border frame.
    Rect2 drawingArea;            ///< Area inside the frame left for views.
    std::vector<Primitive> primitives;
    std::vector<PlacedView> views;
};

/// Creates a sheet with border frame, title block (bottom-right, 180 x 28 mm or the
/// frame width if narrower) and calibration mark. The calibration block sits to the
/// left of the title block when at least 115 mm are free there, otherwise above it;
/// `drawingArea` is what remains above. Element ids: "sheet/frame",
/// "sheet/title.<project|title|author|date|scale|sheet>[.label]", "sheet/titleblock",
/// "sheet/calibration.line" (exactly 100 mm long), "sheet/calibration.tick",
/// "sheet/calibration.rect" (exactly 100 x 50 mm), "sheet/calibration.label",
/// "sheet/calibration.note".
/// Fails (InvalidArgument) when paper or margins are invalid or leave less than
/// 40 x 40 mm for drawing.
core::Result<Sheet> makeSheet(const SheetOptions& options, const Scale& scale, std::string id = "sheet");

} // namespace fos::drawings
