// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/printing/print.hpp"

#include "test_support.hpp"

#include <QPageSize>
#include <QPrinter>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <cmath>

using namespace fos;
using namespace fos::printing;
using namespace fos::drawings;

namespace {

constexpr double kPtPerMm = 72.0 / 25.4;

/// A QPrinter that writes a PDF file: exercises the real QPrinter code path headless.
void setupPdfPrinter(QPrinter& printer, const std::filesystem::path& file, QPageSize::PageSizeId paper) {
    printer.setOutputFormat(QPrinter::PdfFormat);
    printer.setOutputFileName(QString::fromStdString(file.string()));
    printer.setPageSize(QPageSize(paper));
    printer.setPageOrientation(QPageLayout::Portrait);
}

TEST(PlanPagePreview, FitToPageScalesByExactlyTheReportedFactor) {
    // A3 landscape on A4 with 5 mm margins: factor 194/297 = 0.653199 (see plan tests).
    const Sheet a3 = test::standardSheet(PaperFormat::A3, PaperOrientation::Landscape);
    const auto plan = planPrint(a3, PrinterPageInfo::withUniformMargin(210, 297, 5), PrintMode::FitToPage);
    ASSERT_TRUE(plan.ok());
    const PrintPage& pg = plan.value().pages[0];
    const QImage img = renderPlanPagePreview(a3, plan.value(), 0, 10.0);
    ASSERT_EQ(img.size(), QSize(2970, 2100)); // landscape A4 paper at 10 px/mm

    // Calibration line through the page transform: paperY = offsetY + s x (297 - y).
    const drawings::Line cal = test::calibrationLine(a3);
    const double s = plan.value().scaleFactor;
    const double yPaper = pg.offsetY + s * (a3.height - cal.a.y);
    const double xPaper = pg.offsetX + s * cal.a.x;
    const test::Run run = test::longestRun(img, static_cast<int>(std::lround(yPaper * 10)));
    std::printf("[measured] fit-to-page preview: calibration line %d px = %.2f mm on paper (expected %.2f), factor %.4f\n",
                run.length, run.length / 10.0, 100 * s, s);
    EXPECT_NEAR(run.length, 1000 * s, 1.5); // 653.2 px
    EXPECT_NEAR(run.start, xPaper * 10, 1.5);

    // The "not to scale" stamp is really painted: ink in the reserved strip (y 199..205 mm),
    // which the scaled sheet (ends at y = 199 mm) does not reach.
    EXPECT_GT(test::inkBox(img, 50, 1992, 1500, 2050).count, 300);
    // Nothing is painted in the unprintable margin.
    EXPECT_TRUE(test::inkBox(img, 0, 0, 2970, 50).empty());
    EXPECT_TRUE(test::inkBox(img, 0, 0, 50, 2100).empty());

    EXPECT_TRUE(renderPlanPagePreview(a3, plan.value(), 1, 10.0).isNull()); // no such page
    EXPECT_TRUE(renderPlanPagePreview(a3, plan.value(), 0, 0.0).isNull());
}

TEST(PlanPagePreview, TilesAreClippedAndContinueAcrossPages) {
    // 500 x 100 mm sheet with a full-width line at y = 50 and a dot at x = 300.
    // A4 landscape paper, 5 mm unprintable, 5 mm tile margin, 10 mm overlap:
    // content 277 x 190, step 267 -> cols = ceil(490 / 267) = 2, rows = 1.
    Sheet s = test::blankSheet(500, 100);
    s.primitives.emplace_back(drawings::Line{{0, 50}, {500, 50}, LineStyle::Visible, 0.4, {}, {}});
    s.primitives.emplace_back(Marker{MarkerKind::Dot, {300, 80}, 0, 4.0, {}, {}});
    PrintPlanOptions o;
    o.allowTiling = true;
    o.allowRotatePaper = false;
    const auto plan = planPrint(s, PrinterPageInfo::withUniformMargin(297, 210, 5), PrintMode::ExactScale, o);
    ASSERT_TRUE(plan.ok()) << plan.error().message;
    ASSERT_EQ(plan.value().pages.size(), 2u);

    // Page 1/1: sheet x 0..277 at paper x 10..287; the line row is paper y = 10 + 50 = 60 mm.
    const QImage p1 = renderPlanPagePreview(s, plan.value(), 0, 10.0);
    const test::Run r1 = test::longestRun(p1, 600);
    EXPECT_NEAR(r1.start, 100, 1);
    EXPECT_NEAR(r1.length, 2770, 1); // clipped at the tile edge although the line continues
    // The dot (sheet x = 300) is outside tile 1: no ink at its would-be position, row y = 10 + 20 = 30 mm.
    EXPECT_TRUE(test::inkBox(p1, 0, 250, 2970, 350).empty());

    // Page 1/2: sheet x 267..500 (233 mm) at paper x 10..243; dot at paper x = 10 + (300 - 267) = 43 mm, y = 30 mm.
    const QImage p2 = renderPlanPagePreview(s, plan.value(), 1, 10.0);
    const test::Run r2 = test::longestRun(p2, 600);
    EXPECT_NEAR(r2.start, 100, 1);
    EXPECT_NEAR(r2.length, 2330, 1);
    const test::InkBox dot = test::inkBox(p2, 0, 250, 2970, 350);
    ASSERT_FALSE(dot.empty());
    EXPECT_NEAR((dot.minX + dot.maxX + 1) / 2.0, 430.0, 1.0);
    EXPECT_NEAR((dot.minY + dot.maxY + 1) / 2.0, 300.0, 1.0);

    // Crop marks and the label are painted in the tile margin (below the tile: y 110..115 mm).
    EXPECT_GT(test::inkBox(p2, 0, 1101, 2970, 1150).count, 50);
    // Top-left crop mark: vertical stroke at x = 10 mm from y = 5.5 to 9.5 mm.
    EXPECT_TRUE(test::isInk(p1, 100, 75) || test::isInk(p1, 99, 75));
}

TEST(PrintSheets, ExactScaleToPdfPrinterKeepsSheetSizeAndScale) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const std::filesystem::path file = std::filesystem::path(dir.path().toStdString()) / "printed.pdf";
    const Sheet a4 = test::standardSheet(PaperFormat::A4, PaperOrientation::Portrait);
    const Sheet a3 = test::standardSheet(PaperFormat::A3, PaperOrientation::Landscape);
    std::vector<PrintPlan> plans;
    {
        QPrinter printer(QPrinter::HighResolution);
        setupPdfPrinter(printer, file, QPageSize::Letter); // deliberately the wrong paper
        const auto r = printSheets(printer, {a4, a3}, {}, &plans);
        ASSERT_TRUE(r.ok()) << r.error().message;
        EXPECT_TRUE(printer.fullPage());
    }
    ASSERT_EQ(plans.size(), 2u);
    for (const PrintPlan& p : plans) {
        EXPECT_EQ(p.scaleFactor, 1.0);
        EXPECT_EQ(p.pages.size(), 1u);
        EXPECT_FALSE(p.tiled);
    }
    // The paper was switched to the sheet size and orientation for each sheet.
    EXPECT_NEAR(plans[0].page.paperWidth, 210.0, 0.5);
    EXPECT_NEAR(plans[0].page.paperHeight, 297.0, 0.5);
    EXPECT_NEAR(plans[1].page.paperWidth, 420.0, 0.5);
    EXPECT_NEAR(plans[1].page.paperHeight, 297.0, 0.5);

    const test::PdfInfo pdf = test::readPdf(file);
    EXPECT_EQ(pdf.pageCount, 2);
    ASSERT_EQ(pdf.mediaBoxes.size(), 2u);
    EXPECT_NEAR(pdf.mediaBoxes[0][2], 210 * kPtPerMm, 0.5);
    EXPECT_NEAR(pdf.mediaBoxes[0][3], 297 * kPtPerMm, 0.5);
    EXPECT_NEAR(pdf.mediaBoxes[1][2], 420 * kPtPerMm, 0.5);
    EXPECT_NEAR(pdf.mediaBoxes[1][3], 297 * kPtPerMm, 0.5);
    EXPECT_FALSE(pdf.hasImage);

    if (const std::optional<QImage> img = test::rasterizeWithPoppler(file, 1, 254)) {
        const drawings::Line cal = test::calibrationLine(a4);
        const double yPaper = plans[0].pages[0].offsetY + (a4.height - cal.a.y) + 0.1;
        const test::Run run = test::longestRun(*img, static_cast<int>(std::floor(yPaper * 10)));
        std::printf("[measured] QPrinter(PDF) page 1 via pdftocairo @254 dpi: calibration line %d px (= %.1f mm)\n",
                    run.length, run.length / 10.0);
        EXPECT_NEAR(run.length, 1000, 2);
    }
}

TEST(PrintSheets, TilesA1OntoA4Pages) {
    QTemporaryDir dir;
    const std::filesystem::path file = std::filesystem::path(dir.path().toStdString()) / "tiled.pdf";
    const Sheet a1 = test::standardSheet(PaperFormat::A1, PaperOrientation::Portrait);
    PrintOptions o;
    o.matchPaperToSheet = false; // stay on the A4 paper the "user" selected
    o.plan.allowTiling = true;
    std::vector<PrintPlan> plans;
    PrinterPageInfo info;
    {
        QPrinter printer(QPrinter::HighResolution);
        setupPdfPrinter(printer, file, QPageSize::A4);
        printer.setFullPage(true);
        info = printerPageInfo(printer);
        // Without tiling the same job is refused and nothing is written.
        PrintOptions exact = o;
        exact.plan.allowTiling = false;
        const auto refused = printSheets(printer, {a1}, exact);
        ASSERT_FALSE(refused.ok());
        EXPECT_EQ(refused.error().code, core::ErrorCode::ConstraintViolation);

        const auto r = printSheets(printer, {a1}, o, &plans);
        ASSERT_TRUE(r.ok()) << r.error().message;
    }
    EXPECT_NEAR(info.paperWidth, 210.0, 0.2);
    EXPECT_NEAR(info.paperHeight, 297.0, 0.2);
    ASSERT_EQ(plans.size(), 1u);
    // Expected page count from the printable area the PDF printer reports (pw x ph, both
    // orientations), with the documented formula: content = printable - 10, step = content - 10.
    const auto count = [](double pw, double ph) {
        const double cw = pw - 10, ch = ph - 10;
        return static_cast<int>(std::ceil((594 - 10) / (cw - 10) - 1e-9)) *
               static_cast<int>(std::ceil((841 - 10) / (ch - 10) - 1e-9));
    };
    const int expected = std::min(count(info.printable.width, info.printable.height),
                                  count(info.printable.height, info.printable.width));
    std::printf("[measured] PDF printer printable area %.2f x %.2f mm; A1 tiled onto %zu pages (%d rows x %d cols)\n",
                info.printable.width, info.printable.height, plans[0].pages.size(), plans[0].rows, plans[0].cols);
    EXPECT_EQ(static_cast<int>(plans[0].pages.size()), expected);
    EXPECT_EQ(plans[0].scaleFactor, 1.0);
    const test::PdfInfo pdf = test::readPdf(file);
    EXPECT_EQ(pdf.pageCount, expected);
    ASSERT_FALSE(pdf.mediaBoxes.empty());
    const double w = pdf.mediaBoxes[0][2], h = pdf.mediaBoxes[0][3];
    EXPECT_NEAR(std::min(w, h), 210 * kPtPerMm, 0.5);
    EXPECT_NEAR(std::max(w, h), 297 * kPtPerMm, 0.5);
}

TEST(PrintSheets, RejectsEmptyJob) {
    QPrinter printer;
    printer.setOutputFormat(QPrinter::PdfFormat);
    const auto r = printSheets(printer, {});
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, core::ErrorCode::InvalidArgument);
}

} // namespace
