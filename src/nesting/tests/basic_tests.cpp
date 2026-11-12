// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/nesting/svg.hpp"
#include "test_support.hpp"

#include <cmath>

using namespace fos::nesting;
using namespace fos::nesting::test;
using fos::geometry::Vec2;

namespace {

Job squaresJob(double side, int qty, double spacing) {
    Job job;
    job.sheets.push_back(makeSheet("S", 2000, 1000));
    job.parts.push_back(makePart("sq", rectPoly(side, side), qty, RotationRule::Quarter));
    job.partSpacing = spacing;
    return job;
}

} // namespace

TEST(NestBasic, EightSquaresFillTheSheetExactly) {
    // 2000 x 1000 / (500 x 500) = 4 columns x 2 rows = 8 squares, area 8 * 250000 = 2e6 = sheet area.
    Job job = squaresJob(500, 8, 0);
    job.sheets[0].quantity = 1;
    const NestResult r = nestValid(job);
    ASSERT_EQ(r.sheets.size(), 1u);
    EXPECT_EQ(r.placedCount, 8);
    EXPECT_TRUE(r.unplaced.empty());
    EXPECT_NEAR(r.utilisation, 1.0, 1e-12);
    EXPECT_NEAR(r.sheets[0].usedLength, 2000.0, 1e-9);
    EXPECT_FALSE(r.sheets[0].hasRemnant); // 2000 + spacing 0 leaves nothing.
}

TEST(NestBasic, NinthSquareOpensSecondSheetWithRemnant) {
    const NestResult r = nestValid(squaresJob(500, 9, 0));
    ASSERT_EQ(r.sheets.size(), 2u);
    ASSERT_EQ(r.sheets[1].placements.size(), 1u);
    // The single square of sheet 2 sits at the origin: used length 500, remnant [500, 2000] x [0, 1000].
    EXPECT_NEAR(r.sheets[1].usedLength, 500.0, 1e-9);
    ASSERT_TRUE(r.sheets[1].hasRemnant);
    EXPECT_NEAR(r.sheets[1].remnant.min.x, 500.0, 1e-9);
    EXPECT_NEAR(r.sheets[1].remnant.width(), 1500.0, 1e-9);
    EXPECT_NEAR(r.sheets[1].remnant.height(), 1000.0, 1e-9);
    EXPECT_EQ(r.sheets[1].copy, 1);
    // Utilisation: 9 * 250000 / (2 * 2e6) = 0.5625.
    EXPECT_NEAR(r.utilisation, 0.5625, 1e-12);
}

TEST(NestBasic, TwoRightTrianglesNeedHalfTurn) {
    // Right triangle with legs 1000 (x) and 500 (y): two of them form the 1000 x 500 rectangle only when
    // the second is turned by 180 degrees (hypotenuses coincide).
    Job job;
    job.sheets.push_back(makeSheet("S", 1000, 500, 1));
    job.parts.push_back(makePart("tri", poly({{0, 0}, {1000, 0}, {0, 500}}), 2, RotationRule::Half));
    const NestResult r = nestValid(job);
    ASSERT_EQ(r.sheets.size(), 1u);
    ASSERT_EQ(r.placedCount, 2);
    EXPECT_NEAR(r.utilisation, 1.0, 1e-12);
    const Placement& b = r.sheets[0].placements[1];
    EXPECT_DOUBLE_EQ(b.rotationDeg, 180.0);
    // Rotated by 180 the triangle spans [-1000, 0] x [-500, 0]; translating by (1000, 500) fills the sheet.
    EXPECT_NEAR(b.translation.x, 1000.0, 1e-9);
    EXPECT_NEAR(b.translation.y, 500.0, 1e-9);

    job.parts[0].rotation = RotationRule::None;
    const NestResult r0 = nestValid(job);
    EXPECT_EQ(r0.placedCount, 1);
    ASSERT_EQ(r0.unplaced.size(), 1u);
    EXPECT_EQ(r0.unplaced[0].reason, UnplacedReason::StockExhausted);
}

TEST(NestBasic, LShapesInterlock) {
    // L = [0,500]x[0,250] u [0,250]x[250,500], area 125000 + 62500 = 187500. Turned by 180 degrees and
    // translated by (750, 500) it becomes [250,750]x[250,500] u [500,750]x[0,250], the exact complement in
    // the 750 x 500 rectangle. Interlocked used length 750; side by side it would be 1000.
    const auto L = poly({{0, 0}, {500, 0}, {500, 250}, {250, 250}, {250, 500}, {0, 500}});
    Job job;
    job.sheets.push_back(makeSheet("S", 2000, 500, 1));
    job.parts.push_back(makePart("L", L, 2, RotationRule::Half));
    const NestResult r = nestValid(job);
    ASSERT_EQ(r.placedCount, 2);
    EXPECT_NEAR(r.sheets[0].usedLength, 750.0, 1e-9);

    // Exact 750 x 500 sheet: utilisation 2 * 187500 / 375000 = 1.
    job.sheets[0].length = 750;
    const NestResult tight = nestValid(job);
    ASSERT_EQ(tight.placedCount, 2);
    EXPECT_NEAR(tight.utilisation, 1.0, 1e-12);

    // Without rotation the second L (bbox 500 x 500, so x in [0, 250]) always overlaps the first one's
    // bottom arm [0,500]x[0,250]: only one fits.
    job.parts[0].rotation = RotationRule::None;
    const NestResult none = nestValid(job);
    EXPECT_EQ(none.placedCount, 1);
}

TEST(NestBasic, SpacingIsHonouredExactlyAndReducesCount) {
    // Spacing 10: a row of n squares of 500 needs 500n + 10(n-1) <= 2000 -> n = 3 (4 would need 2030);
    // two rows need 1010 > 1000. Any two squares with overlapping x-projections would need 1010 in y, so at
    // most 3 per sheet: 8 squares -> 3 + 3 + 2 on 3 sheets; last sheet used length 500 + 10 + 500 = 1010.
    const NestResult r = nestValid(squaresJob(500, 8, 10));
    ASSERT_EQ(r.sheets.size(), 3u);
    EXPECT_EQ(r.sheets[0].placements.size(), 3u);
    EXPECT_EQ(r.sheets[1].placements.size(), 3u);
    EXPECT_EQ(r.sheets[2].placements.size(), 2u);
    EXPECT_NEAR(r.sheets[2].usedLength, 1010.0, 1e-6);
    // Placed exactly at the spacing, not farther.
    EXPECT_NEAR(minPairDistance(r.sheets[0]), 10.0, 1e-6);
    ASSERT_TRUE(r.sheets[2].hasRemnant);
    EXPECT_NEAR(r.sheets[2].remnant.min.x, 1020.0, 1e-6); // used length + spacing.

    const NestResult r0 = nestValid(squaresJob(500, 8, 0));
    EXPECT_EQ(r0.sheets.size(), 1u);
}

TEST(NestBasic, SpacingAppliesToNonRectangularOutlines) {
    // Two triangles with spacing 5 cannot share the 1000 x 500 sheet (they need the exact fit).
    Job job;
    job.sheets.push_back(makeSheet("S", 1000, 500, 1));
    job.parts.push_back(makePart("tri", poly({{0, 0}, {1000, 0}, {0, 500}}), 2, RotationRule::Half));
    job.partSpacing = 5;
    const NestResult r = nestValid(job);
    EXPECT_EQ(r.placedCount, 1);
    // On a longer sheet the half-turned triangle slides along the hypotenuse until its distance is 5 mm.
    job.sheets[0].length = 2000;
    const NestResult r2 = nestValid(job);
    ASSERT_EQ(r2.placedCount, 2);
    EXPECT_NEAR(minPairDistance(r2.sheets[0]), 5.0, 1e-6);
    // Moving along x by d changes the hypotenuse distance by d * 500 / sqrt(1000^2 + 500^2) = d / sqrt(5):
    // d = 5 * sqrt(5) = 11.18 -> used length 1011.18.
    EXPECT_NEAR(r2.sheets[0].usedLength, 1000.0 + 5.0 * std::sqrt(5.0), 1e-6);
}

TEST(NestBasic, MarginsAndEdgeSpacing) {
    // Squares 490 on 2000 x 1000 with margins 10: area 1980 x 980 -> 4 x 2 = 8 per sheet (1960 <= 1980,
    // 980 <= 980). With an extra edge spacing of 1 mm: 1978 x 978 -> only one row -> 4 per sheet.
    Job job = squaresJob(490, 8, 0);
    job.sheets[0].margins = {10, 10, 10, 10};
    const NestResult r = nestValid(job);
    ASSERT_EQ(r.sheets.size(), 1u);
    EXPECT_NEAR(r.sheets[0].placements[0].outline[0].x, 10.0, 1e-9);
    EXPECT_NEAR(r.sheets[0].placements[0].outline[0].y, 10.0, 1e-9);
    for (const auto& p : r.sheets[0].placements)
        for (auto v : p.outline.vertices) {
            EXPECT_GE(v.x, 10.0 - 1e-9);
            EXPECT_LE(v.x, 1990.0 + 1e-9);
            EXPECT_GE(v.y, 10.0 - 1e-9);
            EXPECT_LE(v.y, 990.0 + 1e-9);
        }
    job.edgeSpacing = 1;
    const NestResult r1 = nestValid(job);
    ASSERT_EQ(r1.sheets.size(), 2u);
    EXPECT_EQ(r1.sheets[0].placements.size(), 4u);
    EXPECT_NEAR(r1.sheets[0].placements[0].outline[0].x, 11.0, 1e-9);
}

TEST(NestBasic, SvgHasExactViewBoxAndEscapedLabels) {
    Job job = squaresJob(500, 2, 0);
    job.parts[0].label = "Door <A&B>";
    job.sheets[0].margins = {5, 5, 5, 5};
    const NestResult r = nestValid(job);
    ASSERT_EQ(r.sheets.size(), 1u);
    const std::string svg = renderNestSvg(job, r.sheets[0]);
    EXPECT_NE(svg.find("viewBox=\"0 0 2000 1000\""), std::string::npos);
    EXPECT_NE(svg.find("width=\"2000mm\" height=\"1000mm\""), std::string::npos);
    EXPECT_NE(svg.find("Door &lt;A&amp;B&gt;"), std::string::npos);
    EXPECT_EQ(svg.find("<A&B>"), std::string::npos);
    EXPECT_NE(svg.find("fos-margin"), std::string::npos);
    EXPECT_NE(svg.find("fos-remnant"), std::string::npos);
    // First square at the placement-area corner (5, 5): svg y is flipped (1000 - 5 = 995).
    EXPECT_NE(svg.find("M 5 995"), std::string::npos);
    EXPECT_EQ(xmlEscape("'\""), "&apos;&quot;");
}
