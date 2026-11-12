// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/drawings/sheet.hpp"

#include <QImage>
#include <QString>

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace fos::printing::test {

/// A real sheet from drawings::makeSheet (frame, title block, calibration mark).
drawings::Sheet standardSheet(drawings::PaperFormat format, drawings::PaperOrientation orientation,
                              drawings::CalibrationKind calibration = drawings::CalibrationKind::Line100);
/// An empty sheet of the given size.
drawings::Sheet blankSheet(double width, double height, std::string id = "test");
/// The "sheet/calibration.line" primitive (asserts that it exists and is 100 mm long).
drawings::Line calibrationLine(const drawings::Sheet& sheet);

bool isInk(const QImage& image, int x, int y); ///< Darker than mid grey.

struct Run {
    int start = 0, length = 0;
};
/// Longest horizontal run of ink pixels in row `y` within [x0, x1).
Run longestRun(const QImage& image, int y, int x0 = 0, int x1 = -1);
/// All horizontal ink runs in row `y` within [x0, x1).
std::vector<Run> runs(const QImage& image, int y, int x0, int x1);

struct InkBox {
    int minX = 0, minY = 0, maxX = -1, maxY = -1, count = 0;
    bool empty() const { return count == 0; }
};
/// Bounding box (inclusive pixel indices) and number of ink pixels inside the window.
InkBox inkBox(const QImage& image, int x0 = 0, int y0 = 0, int x1 = -1, int y1 = -1);

struct PdfInfo {
    std::size_t bytes = 0;
    int pageCount = 0;                              ///< Number of "/Type /Page" objects.
    std::vector<std::array<double, 4>> mediaBoxes;  ///< In file order, points.
    bool hasImage = false;                          ///< Any "/Subtype /Image" XObject.
    bool hasFont = false;                           ///< Any "/Type /Font" object (text is real text).
    std::string raw;
};
/// Minimal PDF reader: scans the (uncompressed) object dictionaries of the file.
PdfInfo readPdf(const std::filesystem::path& file);

/// Renders page `page` (1-based) of a PDF with poppler's pdftocairo at `dpi`;
/// nullopt when pdftocairo is not installed or fails.
std::optional<QImage> rasterizeWithPoppler(const std::filesystem::path& pdf, int page, int dpi);

} // namespace fos::printing::test
