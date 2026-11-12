// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/printing/print_plan.hpp"

#include "test_support.hpp"

#include <gtest/gtest.h>

#include <algorithm>

using namespace fos;
using namespace fos::printing;
using drawings::PaperFormat;
using drawings::PaperOrientation;

namespace {

bool hasCode(const PrintPlan& plan, const std::string& code) {
    return std::any_of(plan.warnings.begin(), plan.warnings.end(),
                       [&](const core::Diagnostic& d) { return d.code == code; });
}
const core::Diagnostic& byCode(const PrintPlan& plan, const std::string& code) {
    static const core::Diagnostic none;
    for (const core::Diagnostic& d : plan.warnings)
        if (d.code == code) return d;
    return none;
}

TEST(PlanPrint, ExactScaleFitsSamePaper) {
    const drawings::Sheet a4 = test::standardSheet(PaperFormat::A4, PaperOrientation::Portrait);
    const auto plan = planPrint(a4, PrinterPageInfo::withUniformMargin(210, 297, 0), PrintMode::ExactScale);
    ASSERT_TRUE(plan.ok()) << plan.error().message;
    const PrintPlan& p = plan.value();
    EXPECT_EQ(p.scaleFactor, 1.0);
    EXPECT_FALSE(p.tiled);
    EXPECT_FALSE(p.paperRotated);
    ASSERT_EQ(p.pages.size(), 1u);
    EXPECT_EQ(p.pages[0].scale, 1.0);
    EXPECT_NEAR(p.pages[0].offsetX, 0.0, 1e-9);
    EXPECT_NEAR(p.pages[0].offsetY, 0.0, 1e-9);
    EXPECT_NEAR(p.pages[0].clip.width, 210.0, 1e-9);
    EXPECT_NEAR(p.pages[0].clip.height, 297.0, 1e-9);
    EXPECT_TRUE(p.pages[0].texts.empty()); // no stamp at exact scale
    EXPECT_FALSE(hasCode(p, "printing.printable_smaller"));
    // Always-present warnings.
    EXPECT_TRUE(hasCode(p, "printing.driver_scaling"));
    EXPECT_NE(byCode(p, "printing.driver_scaling").message.find("fit to page"), std::string::npos);
    EXPECT_TRUE(p.calibrationMarkPresent);
    EXPECT_TRUE(hasCode(p, "printing.calibration.present"));
    EXPECT_DOUBLE_EQ(p.calibrationPrintedLength, 100.0);
}

TEST(PlanPrint, ExactScalePrintableSmallerThanSheetButContentFits) {
    // A4 sheet on A4 paper with 5 mm unprintable margins: printable 200 x 287 < 210 x 297.
    // The sheet frame sits 20/10/10/10 mm inside the paper, so all ink is printable.
    const drawings::Sheet a4 = test::standardSheet(PaperFormat::A4, PaperOrientation::Portrait);
    const auto plan = planPrint(a4, PrinterPageInfo::withUniformMargin(210, 297, 5), PrintMode::ExactScale);
    ASSERT_TRUE(plan.ok()) << plan.error().message;
    const PrintPlan& p = plan.value();
    ASSERT_EQ(p.pages.size(), 1u);
    EXPECT_EQ(p.scaleFactor, 1.0);
    EXPECT_NEAR(p.pages[0].offsetX, 0.0, 1e-9); // not shifted
    EXPECT_NEAR(p.pages[0].offsetY, 0.0, 1e-9);
    EXPECT_NEAR(p.pages[0].clip.x, 5.0, 1e-9);
    EXPECT_NEAR(p.pages[0].clip.width, 200.0, 1e-9);
    EXPECT_NEAR(p.pages[0].clip.height, 287.0, 1e-9);
    ASSERT_TRUE(hasCode(p, "printing.printable_smaller"));
    const std::string& msg = byCode(p, "printing.printable_smaller").message;
    EXPECT_NE(msg.find("200 x 287 mm"), std::string::npos) << msg;
    EXPECT_NE(msg.find("210 x 297 mm"), std::string::npos) << msg;
    EXPECT_FALSE(hasCode(p, "printing.shifted"));
}

TEST(PlanPrint, ExactScaleShiftsContentIntoPrintableArea) {
    // Ink from x = 2 mm; with 0.35 mm line allowance its left edge is at 1.65 mm, the
    // printable area starts at 5 mm -> shift right by 5 - 1.65 = 3.35 mm.
    drawings::Sheet s = test::blankSheet(210, 297);
    s.primitives.emplace_back(drawings::Line{{2, 150}, {100, 150}, drawings::LineStyle::Visible, 0.5, {}, {}});
    const auto plan = planPrint(s, PrinterPageInfo::withUniformMargin(210, 297, 5), PrintMode::ExactScale);
    ASSERT_TRUE(plan.ok()) << plan.error().message;
    EXPECT_NEAR(plan.value().pages[0].offsetX, 3.35, 1e-9);
    EXPECT_NEAR(plan.value().pages[0].offsetY, 0.0, 1e-9);
    EXPECT_EQ(plan.value().scaleFactor, 1.0);
    EXPECT_TRUE(hasCode(plan.value(), "printing.shifted"));
    EXPECT_TRUE(hasCode(plan.value(), "printing.calibration.absent"));
    EXPECT_FALSE(plan.value().calibrationMarkPresent);
    EXPECT_EQ(plan.value().calibrationPrintedLength, 0.0);
}

TEST(PlanPrint, ExactScaleTooLargeIsAnError) {
    const drawings::Sheet a3 = test::standardSheet(PaperFormat::A3, PaperOrientation::Landscape);
    const auto plan = planPrint(a3, PrinterPageInfo::withUniformMargin(210, 297, 5), PrintMode::ExactScale);
    ASSERT_FALSE(plan.ok());
    EXPECT_EQ(plan.error().code, core::ErrorCode::ConstraintViolation);
    EXPECT_NE(plan.error().message.find("420 x 297 mm"), std::string::npos) << plan.error().message;
    EXPECT_NE(plan.error().message.find("200 x 287 mm"), std::string::npos) << plan.error().message;
    EXPECT_NE(plan.error().message.find("tiling"), std::string::npos) << plan.error().message;
}

TEST(PlanPrint, TilingA1OnA4PortraitOnly) {
    // A1 portrait 594 x 841 on A4 portrait, 5 mm unprintable: printable 200 x 287.
    // Tile content = printable - 2 x 5 mm tile margin = 190 x 277; overlap 10 -> step 180 x 267.
    // cols = ceil((594 - 10) / 180) = ceil(3.244) = 4;  rows = ceil((841 - 10) / 267) = ceil(3.112) = 4.
    const drawings::Sheet a1 = test::standardSheet(PaperFormat::A1, PaperOrientation::Portrait);
    PrintPlanOptions o;
    o.allowTiling = true;
    o.allowRotatePaper = false;
    const auto plan = planPrint(a1, PrinterPageInfo::withUniformMargin(210, 297, 5), PrintMode::ExactScale, o);
    ASSERT_TRUE(plan.ok()) << plan.error().message;
    const PrintPlan& p = plan.value();
    EXPECT_TRUE(p.tiled);
    EXPECT_EQ(p.cols, 4);
    EXPECT_EQ(p.rows, 4);
    ASSERT_EQ(p.pages.size(), 16u);
    EXPECT_EQ(p.scaleFactor, 1.0);
    // Last tile (4/4): x0 = 3 x 180 = 540 -> width 594 - 540 = 54; y0 = 3 x 267 = 801 -> height 40.
    const PrintPage& last = p.pages.back();
    EXPECT_EQ(last.label, "4/4");
    EXPECT_NEAR(last.sheetRegion.x, 540.0, 1e-9);
    EXPECT_NEAR(last.sheetRegion.width, 54.0, 1e-9);
    EXPECT_NEAR(last.sheetRegion.y, 801.0, 1e-9);
    EXPECT_NEAR(last.sheetRegion.height, 40.0, 1e-9);
    EXPECT_TRUE(hasCode(p, "printing.tiled"));
    EXPECT_NE(byCode(p, "printing.tiled").message.find("16 pages"), std::string::npos);
}

TEST(PlanPrint, TilingA1OnA4ChoosesOrientationWithFewerPages) {
    // Landscape A4: printable 287 x 200, content 277 x 190, step 267 x 180.
    // cols = ceil(584 / 267) = ceil(2.187) = 3;  rows = ceil(831 / 180) = ceil(4.617) = 5 -> 15 < 16.
    const drawings::Sheet a1 = test::standardSheet(PaperFormat::A1, PaperOrientation::Portrait);
    PrintPlanOptions o;
    o.allowTiling = true;
    const auto plan = planPrint(a1, PrinterPageInfo::withUniformMargin(210, 297, 5), PrintMode::ExactScale, o);
    ASSERT_TRUE(plan.ok()) << plan.error().message;
    const PrintPlan& p = plan.value();
    EXPECT_TRUE(p.paperRotated);
    EXPECT_EQ(p.page.orientation, PaperOrientation::Landscape);
    EXPECT_NEAR(p.page.paperWidth, 297.0, 1e-9);
    EXPECT_EQ(p.cols, 3);
    EXPECT_EQ(p.rows, 5);
    ASSERT_EQ(p.pages.size(), 15u);
    EXPECT_TRUE(hasCode(p, "printing.paper_rotated"));

    // Page order is row by row; page index 5 = row 2, col 3.
    // x0 = 2 x 267 = 534, width = 594 - 534 = 60; y0 = 180, height 190.
    // Tile content origin on paper = printable origin (5, 5) + tile margin 5 = (10, 10).
    // offset = origin - (x0, y0) = (-524, -170).
    const PrintPage& pg = p.pages[5];
    EXPECT_EQ(pg.row, 2);
    EXPECT_EQ(pg.col, 3);
    EXPECT_EQ(pg.label, "2/3");
    EXPECT_NEAR(pg.sheetRegion.x, 534.0, 1e-9);
    EXPECT_NEAR(pg.sheetRegion.width, 60.0, 1e-9);
    EXPECT_NEAR(pg.sheetRegion.y, 180.0, 1e-9);
    EXPECT_NEAR(pg.sheetRegion.height, 190.0, 1e-9);
    EXPECT_NEAR(pg.offsetX, -524.0, 1e-9);
    EXPECT_NEAR(pg.offsetY, -170.0, 1e-9);
    EXPECT_NEAR(pg.clip.x, 10.0, 1e-9);
    EXPECT_NEAR(pg.clip.y, 10.0, 1e-9);
    EXPECT_NEAR(pg.clip.width, 60.0, 1e-9);
    EXPECT_EQ(pg.scale, 1.0);
    ASSERT_EQ(pg.texts.size(), 1u);
    EXPECT_EQ(pg.texts[0].text, "2/3");
    EXPECT_GT(pg.texts[0].y, pg.clip.bottom()); // label below the tile, in the margin
    EXPECT_GE(pg.marks.size(), 8u);             // 4 corners x 2 crop strokes at least

    // Neighbouring tiles share exactly the overlap: tile 1/1 ends at 277, tile 1/2 starts at 267.
    EXPECT_NEAR(p.pages[0].sheetRegion.right() - p.pages[1].sheetRegion.x, 10.0, 1e-9);
    EXPECT_NEAR(p.pages[0].sheetRegion.bottom() - p.pages[3].sheetRegion.y, 10.0, 1e-9);
    // Tile 1/1 has a registration mark on the join line x = clip.x + step = 10 + 267 = 277.
    const auto& m = p.pages[0].marks;
    EXPECT_TRUE(std::any_of(m.begin(), m.end(), [](const PlanMark& k) {
        return std::abs(k.x1 - 277.0) < 1e-9 && std::abs(k.x2 - 277.0) < 1e-9 && k.y1 < 10.0;
    }));
    // All marks stay inside the printable area (5 .. 292 x 5 .. 205).
    for (const PrintPage& page : p.pages)
        for (const PlanMark& k : page.marks) {
            EXPECT_GE(std::min(k.x1, k.x2), 5.0 - 1e-9);
            EXPECT_LE(std::max(k.x1, k.x2), 292.0 + 1e-9);
            EXPECT_GE(std::min(k.y1, k.y2), 5.0 - 1e-9);
            EXPECT_LE(std::max(k.y1, k.y2), 205.0 + 1e-9);
        }
}

TEST(PlanPrint, TilingLabelTemplateAndZeroOverlap) {
    // 400 x 100 sheet, paper 210 x 297 no unprintable margin, tile margin 5, overlap 0:
    // portrait content 200 x 287 -> cols = ceil(400 / 200) = 2, rows = 1 (100 <= 287).
    drawings::Sheet s = test::blankSheet(400, 100);
    s.primitives.emplace_back(drawings::Line{{0, 50}, {400, 50}, drawings::LineStyle::Visible, 0.5, {}, {}});
    PrintPlanOptions o;
    o.allowTiling = true;
    o.allowRotatePaper = false;
    o.tileOverlap = 0;
    o.tileLabel = "Blatt {row}/{col} von {rows}x{cols}";
    const auto plan = planPrint(s, PrinterPageInfo::withUniformMargin(210, 297, 0), PrintMode::ExactScale, o);
    ASSERT_TRUE(plan.ok()) << plan.error().message;
    ASSERT_EQ(plan.value().pages.size(), 2u);
    EXPECT_EQ(plan.value().pages[1].label, "Blatt 1/2 von 1x2");
    EXPECT_NEAR(plan.value().pages[1].sheetRegion.x, 200.0, 1e-9);
}

TEST(PlanPrint, FitToPageFactorA3OnA4WithMargins) {
    // A3 landscape 420 x 297 on A4 with 5 mm unprintable margins, 6 mm stamp strip.
    // Portrait: printable 200 x 287 -> min(200/420, (287-6)/297) = 0.4762.
    // Landscape: printable 287 x 200 -> min(287/420 = 0.68333, (200-6)/297 = 0.653199) = 0.653199 (chosen).
    const drawings::Sheet a3 = test::standardSheet(PaperFormat::A3, PaperOrientation::Landscape);
    const auto plan = planPrint(a3, PrinterPageInfo::withUniformMargin(210, 297, 5), PrintMode::FitToPage);
    ASSERT_TRUE(plan.ok()) << plan.error().message;
    const PrintPlan& p = plan.value();
    EXPECT_NEAR(p.scaleFactor, 194.0 / 297.0, 1e-12);
    EXPECT_NEAR(p.scaleFactor, 0.6532, 5e-5);
    EXPECT_TRUE(p.paperRotated);
    ASSERT_EQ(p.pages.size(), 1u);
    const PrintPage& pg = p.pages[0];
    EXPECT_EQ(pg.scale, p.scaleFactor);
    // Scaled sheet: 420 x 0.653199 = 274.343 wide, 194 high. Centred in 287 x 194 at (5, 5):
    // offsetX = 5 + (287 - 274.343) / 2 = 11.328, offsetY = 5.
    EXPECT_NEAR(pg.offsetX, 11.328, 1e-3);
    EXPECT_NEAR(pg.offsetY, 5.0, 1e-9);
    EXPECT_NEAR(pg.clip.width, 274.343, 1e-3);
    EXPECT_NEAR(pg.clip.height, 194.0, 1e-9);
    // The stamp is explicit and lies in the reserved strip (y in 199 .. 205), below the sheet.
    ASSERT_EQ(pg.texts.size(), 1u);
    EXPECT_EQ(pg.texts[0].text, "Scaled to 65.3 % \xE2\x80\x94 not to scale");
    EXPECT_GT(pg.texts[0].y, pg.clip.bottom());
    EXPECT_LT(pg.texts[0].y, 205.0);
    ASSERT_TRUE(hasCode(p, "printing.scaled"));
    EXPECT_NE(byCode(p, "printing.scaled").message.find("0.6532"), std::string::npos);
    EXPECT_NE(byCode(p, "printing.scaled").message.find("65.3 %"), std::string::npos);
    // Calibration mark: 100 mm x 0.653199 = 65.32 mm on paper, stated in the warning.
    EXPECT_NEAR(p.calibrationPrintedLength, 65.32, 0.005);
    EXPECT_NE(byCode(p, "printing.calibration.present").message.find("65.3 mm"), std::string::npos);
    EXPECT_TRUE(hasCode(p, "printing.driver_scaling"));
}

TEST(PlanPrint, FitToPageA3OnBorderlessA4IsOneOverRootTwo) {
    // No unprintable margins, no stamp strip: landscape min(297/420 = 0.707143, 210/297 = 0.707071) = 0.7071.
    const drawings::Sheet a3 = test::standardSheet(PaperFormat::A3, PaperOrientation::Landscape);
    PrintPlanOptions o;
    o.stampStrip = 0;
    o.scaledLabel = "\xD0\x9C\xD0\xB0\xD1\x81\xD1\x88\xD1\x82\xD0\xB0\xD0\xB1 {percent} %"; // "Масштаб {percent} %"
    const auto plan = planPrint(a3, PrinterPageInfo::withUniformMargin(210, 297, 0), PrintMode::FitToPage, o);
    ASSERT_TRUE(plan.ok()) << plan.error().message;
    EXPECT_NEAR(plan.value().scaleFactor, 210.0 / 297.0, 1e-12);
    EXPECT_NEAR(plan.value().scaleFactor, 0.7071, 5e-5);
    ASSERT_EQ(plan.value().pages[0].texts.size(), 1u);
    EXPECT_EQ(plan.value().pages[0].texts[0].text, "\xD0\x9C\xD0\xB0\xD1\x81\xD1\x88\xD1\x82\xD0\xB0\xD0\xB1 70.7 %");
    EXPECT_EQ(formatScaleStamp("Scaled to {percent} % \xE2\x80\x94 not to scale", 0.70707),
              "Scaled to 70.7 % \xE2\x80\x94 not to scale");
}

TEST(PlanPrint, FitToPageNeverEnlargesUnlessAsked) {
    const drawings::Sheet a4 = test::standardSheet(PaperFormat::A4, PaperOrientation::Portrait);
    const PrinterPageInfo a3 = PrinterPageInfo::withUniformMargin(297, 420, 5);
    const auto plan = planPrint(a4, a3, PrintMode::FitToPage);
    ASSERT_TRUE(plan.ok());
    EXPECT_EQ(plan.value().scaleFactor, 1.0);
    EXPECT_TRUE(plan.value().pages[0].texts.empty()); // 1:1 -> no "not to scale" stamp
    EXPECT_FALSE(hasCode(plan.value(), "printing.scaled"));

    // Enlarging allowed: printable 287 x 410, strip 6 -> min(287/210 = 1.36667, 404/297 = 1.36027) = 1.36027.
    PrintPlanOptions o;
    o.allowEnlarge = true;
    const auto big = planPrint(a4, a3, PrintMode::FitToPage, o);
    ASSERT_TRUE(big.ok());
    EXPECT_NEAR(big.value().scaleFactor, 404.0 / 297.0, 1e-12);
    EXPECT_EQ(big.value().pages[0].texts[0].text, "Scaled to 136.0 % \xE2\x80\x94 not to scale");
}

TEST(PlanPrint, RotatedPageTurnsMarginsWithThePaper) {
    PrinterPageInfo p;
    p.paperWidth = 210;
    p.paperHeight = 297;
    p.printable = {3, 4, 210 - 3 - 5, 297 - 4 - 12}; // margins left 3, top 4, right 5, bottom 12
    const PrinterPageInfo r = rotated(p);
    EXPECT_EQ(r.orientation, PaperOrientation::Landscape);
    EXPECT_DOUBLE_EQ(r.paperWidth, 297);
    EXPECT_DOUBLE_EQ(r.paperHeight, 210);
    EXPECT_DOUBLE_EQ(r.printable.x, 4);                   // left = old top
    EXPECT_DOUBLE_EQ(r.printable.y, 5);                   // top = old right
    EXPECT_DOUBLE_EQ(r.printable.width, 297 - 4 - 12);    // right = old bottom
    EXPECT_DOUBLE_EQ(r.printable.height, 210 - 5 - 3);    // bottom = old left
    const PrinterPageInfo back = rotated(r);
    EXPECT_DOUBLE_EQ(back.printable.x, 3);
    EXPECT_DOUBLE_EQ(back.printable.y, 4);
    EXPECT_DOUBLE_EQ(back.printable.width, p.printable.width);
    EXPECT_DOUBLE_EQ(back.printable.height, p.printable.height);
    EXPECT_EQ(back.orientation, PaperOrientation::Portrait);
}

TEST(PlanPrint, InvalidInputIsRejectedWithSpecificMessages) {
    const drawings::Sheet a4 = test::standardSheet(PaperFormat::A4, PaperOrientation::Portrait);
    const PrinterPageInfo ok = PrinterPageInfo::withUniformMargin(210, 297, 5);

    PrinterPageInfo outside = ok;
    outside.printable = {5, 5, 210, 287}; // right edge at 215 > 210
    auto r = planPrint(a4, outside, PrintMode::ExactScale);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, core::ErrorCode::InvalidArgument);
    EXPECT_NE(r.error().message.find("outside the paper"), std::string::npos);

    r = planPrint(test::blankSheet(0, 100), ok, PrintMode::ExactScale);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, core::ErrorCode::InvalidArgument);

    PrintPlanOptions neg;
    neg.tileOverlap = -1;
    r = planPrint(a4, ok, PrintMode::ExactScale, neg);
    ASSERT_FALSE(r.ok());
    EXPECT_NE(r.error().message.find("overlap"), std::string::npos);

    // Tile margin 95 mm leaves 10 x 97 mm per page, not more than the 10 mm overlap.
    PrintPlanOptions tight;
    tight.allowTiling = true;
    tight.tileMargin = 95;
    r = planPrint(test::standardSheet(PaperFormat::A1, PaperOrientation::Portrait), ok, PrintMode::ExactScale, tight);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, core::ErrorCode::InvalidArgument);
    EXPECT_NE(r.error().message.find("Tiling is impossible"), std::string::npos);

    PrintPlanOptions strip;
    strip.stampStrip = 300;
    r = planPrint(a4, ok, PrintMode::FitToPage, strip);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, core::ErrorCode::InvalidArgument);
}

} // namespace
