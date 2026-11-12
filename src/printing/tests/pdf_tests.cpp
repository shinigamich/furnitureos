// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/printing/pdf.hpp"

#include "test_support.hpp"

#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <cmath>

using namespace fos;
using namespace fos::printing;
using namespace fos::drawings;

namespace {

constexpr double kPtPerMm = 72.0 / 25.4;

std::filesystem::path inDir(const QTemporaryDir& dir, const char* name) {
    return std::filesystem::path(dir.path().toStdString()) / name;
}

TEST(PdfExport, PageBoxesEqualSheetSizes) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const std::filesystem::path file = inDir(dir, "sheets.pdf");
    Sheet a4 = test::standardSheet(PaperFormat::A4, PaperOrientation::Portrait);
    // Cyrillic text in the PDF as well: "Шкаф".
    a4.primitives.emplace_back(Text{{60, 200}, 5.0, HAlign::Left, VAlign::Baseline, 0,
                                    "\xD0\xA8\xD0\xBA\xD0\xB0\xD1\x84", {}, {}});
    const Sheet a3 = test::standardSheet(PaperFormat::A3, PaperOrientation::Landscape);
    PdfOptions o;
    o.title = "Kitchen drawings";
    const auto r = exportPdf({a4, a3}, file, o);
    ASSERT_TRUE(r.ok()) << r.error().message;

    const test::PdfInfo pdf = test::readPdf(file);
    EXPECT_EQ(pdf.raw.substr(0, 5), "%PDF-");
    EXPECT_GT(pdf.bytes, 3000u);
    EXPECT_EQ(pdf.pageCount, 2);
    ASSERT_EQ(pdf.mediaBoxes.size(), 2u);
    // A4 portrait: 210 x 72/25.4 = 595.28 pt, 297 x 72/25.4 = 841.89 pt.
    // A3 landscape: 420 mm = 1190.55 pt, 297 mm = 841.89 pt.
    for (const auto& b : pdf.mediaBoxes)
        std::printf("[measured] PDF MediaBox [%g %g %g %g] pt\n", b[0], b[1], b[2], b[3]);
    EXPECT_NEAR(pdf.mediaBoxes[0][0], 0.0, 1e-9);
    EXPECT_NEAR(pdf.mediaBoxes[0][1], 0.0, 1e-9);
    EXPECT_NEAR(pdf.mediaBoxes[0][2], 210 * kPtPerMm, 0.5);
    EXPECT_NEAR(pdf.mediaBoxes[0][3], 297 * kPtPerMm, 0.5);
    EXPECT_NEAR(pdf.mediaBoxes[1][2], 420 * kPtPerMm, 0.5);
    EXPECT_NEAR(pdf.mediaBoxes[1][3], 297 * kPtPerMm, 0.5);
    // Vector output: no image XObjects at all, and text is real text with a font.
    EXPECT_FALSE(pdf.hasImage);
    EXPECT_TRUE(pdf.hasFont);
    EXPECT_NE(pdf.raw.find("/Title"), std::string::npos);
    EXPECT_NE(pdf.raw.find("/Creator"), std::string::npos);
}

TEST(PdfExport, CalibrationLineIs100MmWhenRenderedByPoppler) {
    // Independent cross-check with poppler (skipped when pdftocairo is not installed):
    // rasterise at 254 dpi = 10 px/mm and measure the calibration line like in the raster test.
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const std::filesystem::path file = inDir(dir, "cal.pdf");
    const Sheet a4 = test::standardSheet(PaperFormat::A4, PaperOrientation::Portrait);
    const Sheet a3 = test::standardSheet(PaperFormat::A3, PaperOrientation::Landscape);
    ASSERT_TRUE(exportPdf({a4, a3}, file).ok());
    const Sheet* sheets[] = {&a4, &a3};
    for (int page = 1; page <= 2; ++page) {
        const std::optional<QImage> img = test::rasterizeWithPoppler(file, page, 254);
        if (!img) GTEST_SKIP() << "pdftocairo not available";
        const Sheet& s = *sheets[page - 1];
        const drawings::Line cal = test::calibrationLine(s);
        EXPECT_NEAR(img->width(), s.width * 10, 4);   // MediaBox is in whole points
        EXPECT_NEAR(img->height(), s.height * 10, 4);
        // The sheet's top edge is the page's top edge, so rows are counted from the top.
        const int row = static_cast<int>(std::floor((s.height - cal.a.y + 0.1) * 10));
        const test::Run run = test::longestRun(*img, row);
        std::printf("[measured] PDF page %d via pdftocairo @254 dpi: calibration line %d px (= %.1f mm), "
                    "starts at x = %d px (expected %.1f), page %d x %d px\n",
                    page, run.length, run.length / 10.0, run.start, cal.a.x * 10, img->width(), img->height());
        EXPECT_NEAR(run.length, 1000, 2);
        EXPECT_NEAR(run.start, cal.a.x * 10, 2.0);
    }
}

TEST(PdfExport, ResolutionDoesNotChangePageSizeOrScale) {
    QTemporaryDir dir;
    const std::filesystem::path file = inDir(dir, "lowres.pdf");
    PdfOptions o;
    o.resolutionDpi = 300;
    // Custom size 123.4 x 56.7 mm -> 349.8 x 160.7 pt.
    Sheet s = test::blankSheet(123.4, 56.7, "custom");
    s.primitives.emplace_back(drawings::Line{{10, 20}, {110, 20}, LineStyle::Visible, 0.4, {}, {}});
    ASSERT_TRUE(exportPdf({s}, file, o).ok());
    const test::PdfInfo pdf = test::readPdf(file);
    ASSERT_EQ(pdf.mediaBoxes.size(), 1u);
    EXPECT_EQ(pdf.pageCount, 1);
    EXPECT_NEAR(pdf.mediaBoxes[0][2], 123.4 * kPtPerMm, 0.5);
    EXPECT_NEAR(pdf.mediaBoxes[0][3], 56.7 * kPtPerMm, 0.5);
    if (const std::optional<QImage> img = test::rasterizeWithPoppler(file, 1, 254)) {
        // Line 0.1 mm above its centre: row (56.7 - 20 - 0.1) x 10 = 366.
        const test::Run run = test::longestRun(*img, 366);
        std::printf("[measured] 300 dpi PDF via pdftocairo: 100 mm line = %d px at 10 px/mm\n", run.length);
        EXPECT_NEAR(run.length, 1000, 2);
        EXPECT_NEAR(run.start, 100, 2);
    }
}

TEST(PdfExport, FailuresAreReported) {
    QTemporaryDir dir;
    const Sheet a4 = test::standardSheet(PaperFormat::A4, PaperOrientation::Portrait);

    auto r = exportPdf({}, inDir(dir, "none.pdf"));
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, core::ErrorCode::InvalidArgument);

    r = exportPdf({test::blankSheet(-5, 100, "broken")}, inDir(dir, "bad.pdf"));
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, core::ErrorCode::InvalidArgument);
    EXPECT_NE(r.error().message.find("'broken'"), std::string::npos);
    EXPECT_FALSE(std::filesystem::exists(inDir(dir, "bad.pdf")));

    PdfOptions low;
    low.resolutionDpi = 10;
    r = exportPdf({a4}, inDir(dir, "res.pdf"), low);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, core::ErrorCode::InvalidArgument);

    const std::filesystem::path missing = inDir(dir, "no-such-dir") / "x.pdf";
    r = exportPdf({a4}, missing);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, core::ErrorCode::IoError);
    EXPECT_NE(r.error().message.find("x.pdf"), std::string::npos);
}

} // namespace
