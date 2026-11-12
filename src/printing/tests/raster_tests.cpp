// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/printing/paint.hpp"

#include "test_support.hpp"

#include <gtest/gtest.h>

#include <cmath>

using namespace fos;
using namespace fos::printing;
using namespace fos::drawings;

namespace {

constexpr double kPpm = 10.0; // pixels per mm in all raster tests

TEST(RasterPreview, CalibrationLineMeasures1000PixelsAt10PxPerMm) {
    const Sheet sheet = test::standardSheet(PaperFormat::A4, PaperOrientation::Portrait);
    const drawings::Line cal = test::calibrationLine(sheet);
    const QImage img = renderPreview(sheet, kPpm);
    ASSERT_FALSE(img.isNull());
    EXPECT_EQ(img.width(), 2100);  // 210 mm x 10
    EXPECT_EQ(img.height(), 2970); // 297 mm x 10

    // The line is 0.35 mm thick (y +- 0.175 mm); the ticks start at y and go up. Scan one
    // pixel row 0.1 mm BELOW the centre line, where only the calibration line has ink.
    const int row = static_cast<int>(std::floor((sheet.height - cal.a.y + 0.1) * kPpm));
    const test::Run run = test::longestRun(img, row);
    RecordProperty("calibration_px", run.length);
    std::printf("[measured] raster calibration line: %d px at 10 px/mm, starts at x = %d px (expected %.1f)\n",
                run.length, run.start, cal.a.x * kPpm);
    EXPECT_NEAR(run.length, 1000, 1); // 100 mm x 10 px/mm
    EXPECT_NEAR(run.start, cal.a.x * kPpm, 1.0);
}

TEST(RasterPreview, RectanglePixelBoundsAndYFlip) {
    // 100 x 80 mm sheet; rectangle (20,10)-(70,50), line weight 0.4 mm (ink reaches 0.2 mm
    // outside the nominal outline, i.e. exactly 2 px).
    //   left   = (20 - 0.2) x 10 = 198          right  = (70 + 0.2) x 10 - 1 = 701
    //   top    = (80 - 50 - 0.2) x 10 = 298     bottom = (80 - 10 + 0.2) x 10 - 1 = 701
    Sheet s = test::blankSheet(100, 80);
    s.primitives.emplace_back(
        Polyline{{{20, 10}, {70, 10}, {70, 50}, {20, 50}}, true, LineStyle::Visible, 0.4, {}, {}});
    const QImage img = renderPreview(s, kPpm);
    ASSERT_EQ(img.size(), QSize(1000, 800));
    const test::InkBox b = test::inkBox(img);
    std::printf("[measured] rectangle ink box: x %d..%d, y %d..%d (expected 198..701, 298..701)\n", b.minX, b.maxX,
                b.minY, b.maxY);
    EXPECT_NEAR(b.minX, 198, 1);
    EXPECT_NEAR(b.maxX, 701, 1);
    EXPECT_NEAR(b.minY, 298, 1);
    EXPECT_NEAR(b.maxY, 701, 1);
    // The interior is not filled, and mitred corners are solid.
    EXPECT_FALSE(test::isInk(img, 450, 500));
    EXPECT_TRUE(test::isInk(img, 199, 299));
}

TEST(RasterPreview, YIsFlippedBottomOfSheetIsBottomOfImage) {
    // A 4 mm dot at (10, 5): 5 mm above the sheet's BOTTOM edge -> pixel centre (100, 750),
    // radius 20 px, on an 800 px high image.
    Sheet s = test::blankSheet(100, 80);
    s.primitives.emplace_back(Marker{MarkerKind::Dot, {10, 5}, 0, 4.0, {}, {}});
    const QImage img = renderPreview(s, kPpm);
    const test::InkBox b = test::inkBox(img);
    ASSERT_FALSE(b.empty());
    EXPECT_NEAR((b.minX + b.maxX + 1) / 2.0, 100.0, 1.0);
    EXPECT_NEAR((b.minY + b.maxY + 1) / 2.0, 750.0, 1.0);
    EXPECT_NEAR(b.maxX - b.minX + 1, 40, 2);
    EXPECT_TRUE(test::inkBox(img, 0, 0, 1000, 400).empty()); // nothing in the upper half
    // Disc area = pi x 20^2 = 1257 px.
    EXPECT_NEAR(b.count, 1257, 40);
}

TEST(RasterPreview, DashPatternIsInMillimetres) {
    // Hidden line: 2 mm dash, 1 mm gap. From x = 10 to 40 mm: dashes at 10-12, 13-15, ... 37-39
    // -> 10 dashes of 20 px separated by 10 px gaps.
    ASSERT_EQ(dashPattern(LineStyle::Hidden), (std::vector<double>{2.0, 1.0}));
    Sheet s = test::blankSheet(100, 80);
    s.primitives.emplace_back(drawings::Line{{10, 40}, {40, 40}, LineStyle::Hidden, 0.4, {}, {}});
    const QImage img = renderPreview(s, kPpm);
    const std::vector<test::Run> r = test::runs(img, 400, 0, 1000);
    ASSERT_EQ(r.size(), 10u);
    for (std::size_t i = 0; i < r.size(); ++i) {
        EXPECT_NEAR(r[i].length, 20, 1) << "dash " << i;
        EXPECT_NEAR(r[i].start, 100 + 30 * static_cast<int>(i), 1) << "dash " << i;
    }
}

TEST(RasterPreview, ArcIsCounterClockwiseOnPaper) {
    // Arc centre (50, 40), r = 20, 0 -> 90 degrees: the paper's upper-right quadrant,
    // i.e. pixels x 500..700, y 200..400 (plus 2 px for the 0.4 mm line).
    Sheet s = test::blankSheet(100, 80);
    s.primitives.emplace_back(Arc{{50, 40}, 20, 0, 90, LineStyle::Visible, 0.4, {}, {}});
    const QImage img = renderPreview(s, kPpm);
    const test::InkBox b = test::inkBox(img);
    ASSERT_FALSE(b.empty());
    EXPECT_NEAR(b.minX, 500, 2);
    EXPECT_NEAR(b.maxX, 701, 2);
    EXPECT_NEAR(b.minY, 198, 2);
    EXPECT_NEAR(b.maxY, 400, 2);
    // Midpoint of the arc at 45 degrees: (50 + 14.142, 40 + 14.142) -> pixel (641, 259).
    EXPECT_TRUE(test::isInk(img, 641, 259));
    // The wrapped arc 90 -> 0 is the other three quadrants.
    Sheet w = test::blankSheet(100, 80);
    w.primitives.emplace_back(Arc{{50, 40}, 20, 90, 0, LineStyle::Visible, 0.4, {}, {}});
    const QImage wrapped = renderPreview(w, kPpm);
    EXPECT_FALSE(test::isInk(wrapped, 641, 259));
    EXPECT_TRUE(test::isInk(wrapped, 359, 541)); // 225 degrees: (50 - 14.142, 40 - 14.142)
    EXPECT_NEAR(test::inkBox(wrapped).minX, 298, 2);
}

TEST(RasterPreview, ArrowMarkerMatchesOutline) {
    // Arrow tip at (50, 40) pointing along +X (angle 0), size 10: length 10, width 10/3.
    // Tail at x = 40 -> pixels x 400..500; half width 1.667 mm -> y 383..417.
    Sheet s = test::blankSheet(100, 80);
    s.primitives.emplace_back(Marker{MarkerKind::Arrow, {50, 40}, 0, 10.0, {}, {}});
    const QImage img = renderPreview(s, kPpm);
    const test::InkBox b = test::inkBox(img);
    ASSERT_FALSE(b.empty());
    EXPECT_NEAR(b.minX, 400, 2);
    EXPECT_NEAR(b.maxX, 499, 3);
    EXPECT_NEAR(b.minY, 383, 2);
    EXPECT_NEAR(b.maxY, 416, 2);
    // Triangle area = 0.5 x 100 px x 33.3 px = 1667 px.
    EXPECT_NEAR(b.count, 1667, 80);
}

TEST(RasterPreview, CyrillicTextRendersRealGlyphs) {
    // "Шкаф кухонный" (13 code points), height 5 mm, baseline-left at (10, 40).
    const std::string cyr = "\xD0\xA8\xD0\xBA\xD0\xB0\xD1\x84 \xD0\xBA\xD1\x83\xD1\x85\xD0\xBE\xD0\xBD\xD0\xBD\xD1\x8B\xD0\xB9";
    Sheet s = test::blankSheet(100, 80);
    s.primitives.emplace_back(Text{{10, 40}, 5.0, HAlign::Left, VAlign::Baseline, 0, cyr, {}, {}});
    const QImage img = renderPreview(s, kPpm);
    // Text area: x from 10 mm, at most ~13 x 5 mm wide; y from 2 mm below to 5 mm above the baseline.
    const test::InkBox b = test::inkBox(img, 90, 350, 800, 420);
    std::printf("[measured] Cyrillic text ink: %d px, box x %d..%d, y %d..%d\n", b.count, b.minX, b.maxX, b.minY,
                b.maxY);
    EXPECT_GT(b.count, 1500); // real glyph strokes, not a couple of stray pixels
    EXPECT_EQ(test::inkBox(img).count, b.count); // and nothing outside the text area
    EXPECT_NEAR(b.minX, 100, 8);
    // Capital "Ш" stands on the baseline (y = 400 px); cap height is about 0.7 x 50 px.
    const test::InkBox sha = test::inkBox(img, 90, 300, 135, 401);
    EXPECT_NEAR(sha.maxY, 399, 2);
    EXPECT_NEAR(400 - sha.minY, 35, 5);

    // Missing-glyph boxes would look identical for different letters: "Ш" and "Ж" must differ.
    const auto letter = [](const char* utf8) {
        Sheet one = test::blankSheet(20, 20);
        one.primitives.emplace_back(Text{{5, 5}, 10.0, HAlign::Left, VAlign::Baseline, 0, utf8, {}, {}});
        return renderPreview(one, kPpm);
    };
    const QImage sh = letter("\xD0\xA8"), zh = letter("\xD0\x96");
    EXPECT_GT(test::inkBox(sh).count, 300);
    EXPECT_GT(test::inkBox(zh).count, 300);
    EXPECT_NE(sh, zh);
}

TEST(RasterPreview, TextAnchorsAndRotationFollowTheSvgConvention) {
    const auto render = [](const Text& t) {
        Sheet s = test::blankSheet(100, 80);
        s.primitives.emplace_back(t);
        return test::inkBox(renderPreview(s, kPpm));
    };
    // "H", height 10 mm. Baseline anchor at (20, 20): the letter stands on y_px = 600 and is
    // one cap height (about 0.7 x 100 px, font dependent) tall; it starts at x_px = 200.
    const test::InkBox base = render({{20, 20}, 10.0, HAlign::Left, VAlign::Baseline, 0, "H", {}, {}});
    ASSERT_FALSE(base.empty());
    EXPECT_NEAR(base.maxY, 599, 2);
    const int cap = base.maxY - base.minY + 1;
    EXPECT_NEAR(cap, 70, 8);
    EXPECT_NEAR(base.minX, 200, 12);
    const int width = base.maxX - base.minX + 1;

    // Middle: baseline shifted down by 0.35 x 100 = 35 px; Top: by 70 px.
    const test::InkBox mid = render({{20, 20}, 10.0, HAlign::Left, VAlign::Middle, 0, "H", {}, {}});
    EXPECT_NEAR(mid.maxY - base.maxY, 35, 1);
    const test::InkBox top = render({{20, 20}, 10.0, HAlign::Left, VAlign::Top, 0, "H", {}, {}});
    EXPECT_NEAR(top.maxY - base.maxY, 70, 1);

    // Right-aligned: ends at the anchor; centred: symmetric about it (side bearings of H are equal).
    const test::InkBox right = render({{50, 20}, 10.0, HAlign::Right, VAlign::Baseline, 0, "H", {}, {}});
    EXPECT_NEAR(right.maxX, 499, 12);
    EXPECT_LT(right.maxX, 500);
    const test::InkBox centre = render({{50, 20}, 10.0, HAlign::Center, VAlign::Baseline, 0, "H", {}, {}});
    EXPECT_NEAR((centre.minX + centre.maxX + 1) / 2.0, 500.0, 1.5);

    // Rotated 90 degrees counter-clockwise about (50, 20): the text runs UP the paper; the
    // baseline is the vertical line x_px = 500 with the letter to its left.
    const test::InkBox rot = render({{50, 20}, 10.0, HAlign::Left, VAlign::Baseline, 90, "H", {}, {}});
    EXPECT_NEAR(rot.maxX, 499, 2);
    EXPECT_NEAR(rot.maxX - rot.minX + 1, cap, 2);
    EXPECT_NEAR(rot.maxY - rot.minY + 1, width, 2);
    EXPECT_LT(rot.maxY, 600);                               // above the anchor (y_px = 600) ...
    EXPECT_NEAR(600 - rot.maxY - 1, base.minX - 200, 2);    // ... by the left side bearing

    // Height is the em size: doubling it doubles the cap height.
    const test::InkBox big = render({{20, 20}, 20.0, HAlign::Left, VAlign::Baseline, 0, "H", {}, {}});
    EXPECT_NEAR(big.maxY - big.minY + 1, 2 * cap, 2);
}

TEST(RasterPreview, InvalidRequestsGiveNullImage) {
    const Sheet s = test::blankSheet(100, 80);
    EXPECT_TRUE(renderPreview(s, 0).isNull());
    EXPECT_TRUE(renderPreview(s, -3).isNull());
    EXPECT_TRUE(renderPreview(s, std::nan("")).isNull());
    EXPECT_TRUE(renderPreview(test::blankSheet(0, 80), kPpm).isNull());
    EXPECT_TRUE(renderPreview(s, 1000).isNull()); // 100000 px wide > kMaxPreviewSidePx
    // Thumbnail: 100 x 80 mm at 0.5 px/mm = 50 x 40 px; fractional sizes round up.
    EXPECT_EQ(renderPreview(s, 0.5).size(), QSize(50, 40));
    EXPECT_EQ(renderPreview(test::blankSheet(100.5, 80), 1.0).size(), QSize(101, 80));
    // Background colour is honoured; bad primitives are skipped without crashing.
    Sheet bad = s;
    bad.primitives.emplace_back(drawings::Line{{std::nan(""), 1}, {2, 3}, LineStyle::Visible, 0.5, {}, {}});
    bad.primitives.emplace_back(Circle{{50, 40}, -1, LineStyle::Visible, 0.5, {}, {}});
    PaintOptions o;
    o.background = QColor(Qt::yellow);
    const QImage img = renderPreview(bad, 2.0, o);
    EXPECT_EQ(img.pixelColor(10, 10), QColor(Qt::yellow));
    EXPECT_TRUE(test::inkBox(img).empty());
}

} // namespace
