// SPDX-License-Identifier: AGPL-3.0-only
// Rotation candidate sets, placement scores, part-in-part (optimiser and validator), restarts/budgets.
#include "test_support.hpp"

#include <cmath>

using namespace fos::nesting;
using namespace fos::nesting::test;
using fos::geometry::Vec2;

namespace {

Job oneSheet(double l, double w, double spacing = 0) {
    Job job;
    job.sheets.push_back(makeSheet("s", l, w));
    job.partSpacing = spacing;
    return job;
}

Part frame(int qty) {
    Part f = makePart("frame", rectPoly(400, 400), qty, RotationRule::None);
    f.holes.push_back(poly({{100, 100}, {300, 100}, {300, 300}, {100, 300}})); // 200 x 200 cut-out
    return f;
}

} // namespace

TEST(NestRotationSet, IntersectsRuleAndKeepsGrainParts) {
    NestOptions opt;
    opt.rotationSetDeg = {0, 90, 180, 270};
    Part grain = makePart("g", rectPoly(10, 5), 1, RotationRule::Half);
    Part step = makePart("st", rectPoly(10, 5), 1, RotationRule::Step);
    step.rotationStepDeg = 30;
    EXPECT_EQ(allowedRotations(grain, opt), (std::vector<double>{0, 180}));
    EXPECT_EQ(allowedRotations(step, opt), (std::vector<double>{0, 90, 180, 270}));
    opt.rotationSetDeg = {90};
    EXPECT_TRUE(allowedRotations(grain, opt).empty());
    Job job = oneSheet(100, 100);
    job.parts.push_back(grain);
    const auto r = nest(job, opt);
    ASSERT_FALSE(r.ok()); // grain part has no allowed angle left
    opt.rotationSetDeg = {400};
    EXPECT_FALSE(validateJob(job, opt).empty());
}

TEST(NestRotationSet, RestrictsPlacements) {
    // 100 x 30 parts on a 50 x 220 placement area only fit rotated by 90 or 270.
    Job job = oneSheet(50, 220);
    job.parts.push_back(makePart("bar", rectPoly(100, 30), 2, RotationRule::Quarter));
    NestOptions opt;
    opt.rotationSetDeg = {270};
    const NestResult r = nestValid(job, opt);
    ASSERT_EQ(r.placedCount, 2);
    for (const auto& p : r.sheets[0].placements) EXPECT_DOUBLE_EQ(p.rotationDeg, 270.0);
    opt.rotationSetDeg = {0, 180};
    const NestResult none = nestValid(job, opt);
    EXPECT_EQ(none.placedCount, 0);
    ASSERT_EQ(none.unplaced.size(), 2u);
    EXPECT_EQ(none.unplaced[0].reason, UnplacedReason::TooLarge);
}

TEST(NestPlacementScore, BottomLeftPacksAlongY) {
    // Two 40 x 40 squares on a 100 x 100 sheet. MinRightEdge puts the second above the first (x = 0,
    // y = 40); BottomLeft puts it right of the first (x = 40, y = 0).
    Job job = oneSheet(100, 100);
    job.parts.push_back(makePart("sq", rectPoly(40, 40), 2));
    NestOptions opt;
    opt.iterations = 0;
    opt.placementScores = {PlacementScore::MinRightEdge};
    NestResult r = nestValid(job, opt);
    ASSERT_EQ(r.sheets.at(0).placements.size(), 2u);
    EXPECT_NEAR(r.sheets[0].placements[1].translation.x, 0, 1e-9);
    EXPECT_NEAR(r.sheets[0].placements[1].translation.y, 40, 1e-9);
    opt.placementScores = {PlacementScore::BottomLeft};
    r = nestValid(job, opt);
    EXPECT_NEAR(r.sheets[0].placements[1].translation.x, 40, 1e-9);
    EXPECT_NEAR(r.sheets[0].placements[1].translation.y, 0, 1e-9);
    EXPECT_EQ(r.placementScoreUsed, PlacementScore::BottomLeft);
    opt.placementScores = {PlacementScore::MinBoundingBox};
    r = nestValid(job, opt);
    EXPECT_EQ(r.placedCount, 2);
    // Used box after the second square: 40 x 80 or 80 x 40 = 3200 mm^2 either way; the tie goes to the
    // smaller right edge (x + w = 40), i.e. stacked at (0, 40).
    EXPECT_NEAR(r.sheets[0].placements[1].translation.x, 0, 1e-9);
}

TEST(NestPartInPart, SmallPartGoesIntoTheCutOut) {
    // Placement area 500 x 400 holds the 400 x 400 frame but not a 150 x 150 block beside it
    // (500 - 400 - 10 = 90 < 150). The 200 x 200 cut-out minus 10 mm spacing on each side leaves
    // 180 x 180, enough for the block.
    Job job = oneSheet(500, 400, 10);
    job.sheets[0].quantity = 1;
    job.parts.push_back(frame(1));
    job.parts.push_back(makePart("block", rectPoly(150, 150), 1));
    NestOptions opt;
    const NestResult off = nestValid(job, opt);
    EXPECT_EQ(off.placedCount, 1); // one sheet in stock: the block stays unplaced
    opt.partInPart = true;
    const NestResult on = nestValid(job, opt);
    ASSERT_EQ(on.placedCount, 2);
    ASSERT_EQ(on.sheets.size(), 1u);
    // Net areas: frame 160 000 - 40 000 = 120 000, block 22 500 -> 142 500 / 200 000 = 0.7125.
    EXPECT_NEAR(on.sheets[0].netUtilisation, 0.7125, 1e-9);
    // Outline areas: 160 000 + 22 500 = 182 500 / 200 000.
    EXPECT_NEAR(on.sheets[0].utilisation, 0.9125, 1e-9);
    // A 190 x 190 block does not fit the 180 x 180 usable cut-out.
    job.parts[1].outline = rectPoly(190, 190);
    const NestResult tight = nestValid(job, opt);
    EXPECT_EQ(tight.placedCount, 1);
}

TEST(NestPartInPart, ValidatorRules) {
    Job job = oneSheet(500, 400, 10);
    job.parts.push_back(frame(1));
    job.parts.push_back(makePart("block", rectPoly(150, 150), 1));
    NestResult r;
    SheetLayout L;
    L.sheetId = "s";
    Placement f{"frame", 0, 0, false, {0, 0}, rectPoly(400, 400)};
    const auto blockAt = [](Vec2 t) {
        return Placement{"block", 0, 0, false, t, fos::geometry::translated(rectPoly(150, 150), t)};
    };
    const auto build = [&](Vec2 t) {
        NestResult out;
        SheetLayout s = L;
        s.placements = {f, blockAt(t)};
        s.partArea = 160000 + 22500;
        s.netPartArea = 120000 + 22500;
        s.sheetArea = 200000;
        s.utilisation = s.partArea / s.sheetArea;
        s.netUtilisation = s.netPartArea / s.sheetArea;
        s.usedLength = 400;
        s.hasRemnant = true; // free strip [400 + 10, 500] x [0, 400]
        s.remnant = fos::geometry::Rect2{{410, 0}, {500, 400}};
        out.sheets.push_back(s);
        out.placedCount = 2;
        out.totalPartArea = s.partArea;
        out.totalNetPartArea = s.netPartArea;
        out.totalSheetArea = s.sheetArea;
        out.utilisation = s.utilisation;
        out.netUtilisation = s.netUtilisation;
        out.sheetLowerBound = 1; // 182 500 / 200 000 -> 1
        return out;
    };
    // Block at (125, 125): 25 mm from every cut-out edge (cut-out 100..300) -> valid.
    EXPECT_TRUE(validateNest(job, build({125, 125})).empty()) << codes(validateNest(job, build({125, 125})));
    // Block at (105, 125): only 5 mm from the cut-out's left edge (< 10 mm spacing) -> spacing error.
    auto d = validateNest(job, build({105, 125}));
    ASSERT_EQ(d.size(), 1u) << codes(d);
    EXPECT_EQ(d[0].code, "nesting.result.spacing");
    // Block at (50, 125) straddles the frame material -> spacing error.
    d = validateNest(job, build({50, 125}));
    ASSERT_FALSE(d.empty());
    EXPECT_EQ(d[0].code, "nesting.result.spacing");
}

TEST(NestRestarts, DeterministicAndBudgeted) {
    Job job = oneSheet(1000, 600, 5);
    job.parts.push_back(makePart("tri", poly({{0, 0}, {300, 0}, {0, 200}}), 10, RotationRule::Quarter));
    job.parts.push_back(makePart("rect", rectPoly(250, 120), 6, RotationRule::Quarter));
    NestOptions opt;
    opt.iterations = 4;
    opt.randomRestarts = 2;
    const NestResult a = nestValid(job, opt);
    const NestResult b = nestValid(job, opt);
    ASSERT_EQ(a.sheets.size(), b.sheets.size());
    for (std::size_t s = 0; s < a.sheets.size(); ++s) {
        ASSERT_EQ(a.sheets[s].placements.size(), b.sheets[s].placements.size());
        for (std::size_t i = 0; i < a.sheets[s].placements.size(); ++i) {
            EXPECT_EQ(a.sheets[s].placements[i].partId, b.sheets[s].placements[i].partId);
            EXPECT_EQ(a.sheets[s].placements[i].translation.x, b.sheets[s].placements[i].translation.x);
            EXPECT_EQ(a.sheets[s].placements[i].translation.y, b.sheets[s].placements[i].translation.y);
        }
    }
    // 3 scores x (4 orders + 2 shuffles), at most 6 distinct starts, + 4 iterations.
    EXPECT_LE(a.decodesRun, 3 * 6 + 4);
    opt.maxDecodes = 2;
    const NestResult c = nestValid(job, opt);
    EXPECT_EQ(c.decodesRun, 2);
    EXPECT_EQ(c.iterationsRun, 0);
    opt.maxDecodes = -1;
    EXPECT_FALSE(nest(job, opt).ok());
}

TEST(NestLowerBound, AreaBound) {
    // 7 squares of 50 x 50 = 17 500 mm^2 on a 100 x 100 placement area -> ceil(1.75) = 2.
    Job job = oneSheet(100, 100);
    job.parts.push_back(makePart("sq", rectPoly(50, 50), 7));
    const NestResult r = nestValid(job);
    EXPECT_EQ(r.sheetLowerBound, 2);
    EXPECT_EQ(r.sheets.size(), 2u);
}
