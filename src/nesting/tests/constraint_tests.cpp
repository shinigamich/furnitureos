// SPDX-License-Identifier: AGPL-3.0-only
#include "test_support.hpp"

#include <cmath>

using namespace fos::nesting;
using namespace fos::nesting::test;
using fos::geometry::Vec2;

namespace {

bool hasCode(const fos::core::Diagnostics& ds, const std::string& code) {
    for (const auto& d : ds)
        if (d.code == code) return true;
    return false;
}

Job rectOnSheet(double w, double h, RotationRule rule, double sl, double sw) {
    Job job;
    job.sheets.push_back(makeSheet("S", sl, sw, 1));
    job.parts.push_back(makePart("p", rectPoly(w, h), 1, rule));
    return job;
}

} // namespace

TEST(NestRotation, RotationRulesAreRespected) {
    // 1200 x 400 on a 1000 x 1500 sheet: only the 90/270 degree orientations (400 x 1200) fit.
    Job none = rectOnSheet(1200, 400, RotationRule::None, 1000, 1500);
    const NestResult r0 = nestValid(none);
    ASSERT_EQ(r0.unplaced.size(), 1u);
    EXPECT_EQ(r0.unplaced[0].reason, UnplacedReason::TooLarge);
    EXPECT_NE(r0.unplaced[0].message.find("1200 x 400 mm at 0 deg"), std::string::npos) << r0.unplaced[0].message;
    EXPECT_NE(r0.unplaced[0].message.find("'S' 1000 x 1500 mm"), std::string::npos) << r0.unplaced[0].message;

    Job half = rectOnSheet(1200, 400, RotationRule::Half, 1000, 1500);
    EXPECT_EQ(nestValid(half).unplaced.size(), 1u); // 180 degrees is still 1200 wide.

    Job quarter = rectOnSheet(1200, 400, RotationRule::Quarter, 1000, 1500);
    const NestResult rq = nestValid(quarter);
    ASSERT_EQ(rq.placedCount, 1);
    EXPECT_DOUBLE_EQ(rq.sheets[0].placements[0].rotationDeg, 90.0);
}

TEST(NestRotation, ArbitraryStep) {
    // 1100 x 100 on 1000 x 1000: at 45 degrees the bbox is (1100 + 100) / sqrt(2) = 848.5 square -> fits;
    // 0 and 90 degrees need 1100.
    Job job = rectOnSheet(1100, 100, RotationRule::Step, 1000, 1000);
    job.parts[0].rotationStepDeg = 45;
    const NestResult r = nestValid(job);
    ASSERT_EQ(r.placedCount, 1);
    EXPECT_DOUBLE_EQ(r.sheets[0].placements[0].rotationDeg, 45.0);
    EXPECT_NEAR(r.sheets[0].usedLength, 1200.0 / std::sqrt(2.0), 1e-9);
    // With the default step from the options (90 here) it does not fit.
    job.parts[0].rotationStepDeg = 0;
    NestOptions opt;
    opt.rotationStepDeg = 90;
    EXPECT_EQ(nestValid(job, opt).unplaced.size(), 1u);
    EXPECT_EQ(allowedRotations(job.parts[0], opt), (std::vector<double>{0, 90, 180, 270}));
}

TEST(NestRotation, MirrorOnlyWhenAllowed) {
    // Non-symmetric quadrilateral: the optimiser never mirrors it when mirroring is not allowed, and the
    // validator rejects a hand-made mirrored placement until mirrorAllowed is set.
    Job job = rectOnSheet(300, 200, RotationRule::None, 600, 300);
    job.parts[0].outline = poly({{0, 0}, {300, 0}, {300, 200}, {100, 100}});
    NestResult r = nestValid(job);
    ASSERT_EQ(r.placedCount, 1);
    EXPECT_FALSE(r.sheets[0].placements[0].mirrored);
    Placement& p = r.sheets[0].placements[0];
    p.mirrored = true;
    p.translation = {300, 0};
    p.outline = fos::geometry::translated(fos::geometry::mirroredX(job.parts[0].outline), {300, 0});
    const auto d = validateNest(job, r);
    EXPECT_TRUE(hasCode(d, "nesting.result.mirror_not_allowed")) << codes(d);
    job.parts[0].mirrorAllowed = true;
    EXPECT_TRUE(validateNest(job, r).empty()) << codes(validateNest(job, r));
}

TEST(NestStock, StockLimitsAndSheetOrder) {
    // 'small' 600 x 600 (1 sheet) is listed first: the first square opens it; after that its free area
    // 360000 - 250000 < 250000. 'big' 2000 x 1000 (1 sheet) takes 8 more. The 10th square: stock exhausted.
    Job job;
    job.sheets.push_back(makeSheet("small", 600, 600, 1));
    job.sheets.push_back(makeSheet("big", 2000, 1000, 1));
    job.parts.push_back(makePart("sq", rectPoly(500, 500), 10, RotationRule::None));
    const NestResult r = nestValid(job);
    ASSERT_EQ(r.sheets.size(), 2u);
    EXPECT_EQ(r.sheets[0].sheetId, "small");
    EXPECT_EQ(r.sheets[1].sheetId, "big");
    EXPECT_EQ(r.placedCount, 9);
    ASSERT_EQ(r.unplaced.size(), 1u);
    EXPECT_EQ(r.unplaced[0].reason, UnplacedReason::StockExhausted);
    EXPECT_EQ(r.unplaced[0].instance, 9);

    job.sheets[0].quantity = 0;
    job.sheets[1].quantity = 0;
    const NestResult r0 = nestValid(job);
    EXPECT_EQ(r0.placedCount, 0);
    EXPECT_EQ(r0.unplaced.size(), 10u);
    EXPECT_DOUBLE_EQ(r0.utilisation, 0.0);
}

TEST(NestStock, OversizeEverywhere) {
    Job job = rectOnSheet(2500, 1500, RotationRule::Quarter, 2000, 1000);
    job.sheets[0].quantity = kUnlimited;
    const NestResult r = nestValid(job);
    ASSERT_EQ(r.unplaced.size(), 1u);
    EXPECT_EQ(r.unplaced[0].reason, UnplacedReason::TooLarge);
    EXPECT_TRUE(r.sheets.empty());
    const std::string& m = r.unplaced[0].message;
    EXPECT_NE(m.find("2500 x 1500 mm at 0 deg"), std::string::npos) << m;
    EXPECT_NE(m.find("1500 x 2500 mm at 90 deg"), std::string::npos) << m;
}

TEST(NestJob, InvalidInputIsReported) {
    Job job = rectOnSheet(100, 100, RotationRule::None, 1000, 1000);
    job.parts.push_back(job.parts[0]);                                           // duplicate id
    job.parts[1].outline = poly({{0, 0}, {100, 100}, {100, 0}, {0, 100}});       // bow-tie
    job.parts[0].holes.push_back(poly({{50, 50}, {150, 50}, {150, 80}}));       // hole leaves outline
    job.sheets[0].margins.left = 600;
    job.sheets[0].margins.right = 500; // nothing left
    job.partSpacing = -1;
    const auto d = validateJob(job);
    EXPECT_TRUE(hasCode(d, "nesting.job.part_id"));
    EXPECT_TRUE(hasCode(d, "nesting.job.outline"));
    EXPECT_TRUE(hasCode(d, "nesting.job.hole"));
    EXPECT_TRUE(hasCode(d, "nesting.job.no_area"));
    EXPECT_TRUE(hasCode(d, "nesting.job.spacing"));
    const auto r = nest(job);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, fos::core::ErrorCode::InvalidArgument);
    EXPECT_FALSE(validateJob(Job{}).empty());
}

TEST(NestJob, HolesCountOnlyForNetArea) {
    // 400 x 400 square with a 100 x 100 hole: net 160000 - 10000 = 150000; outline area 160000.
    Job job = rectOnSheet(400, 400, RotationRule::None, 1000, 1000);
    job.parts[0].holes.push_back(poly({{100, 100}, {200, 100}, {200, 200}, {100, 200}}));
    const NestResult r = nestValid(job);
    EXPECT_DOUBLE_EQ(r.totalPartArea, 160000.0);
    EXPECT_DOUBLE_EQ(r.totalNetPartArea, 150000.0);
}

TEST(NestValidator, DetectsTampering) {
    Job job;
    job.sheets.push_back(makeSheet("S", 2000, 1000, 1));
    job.parts.push_back(makePart("sq", rectPoly(500, 500), 2, RotationRule::Half));
    job.partSpacing = 10;
    const NestResult good = nestValid(job);
    ASSERT_EQ(good.placedCount, 2);
    const auto movedBy = [&](NestResult r, Vec2 d) {
        auto& p = r.sheets[0].placements[1];
        p.translation = p.translation + d;
        p.outline = fos::geometry::translated(p.outline, d);
        return r;
    };
    // Second square sits 10 mm right of the first one; moving it 0.01 mm left breaks the spacing.
    EXPECT_TRUE(hasCode(validateNest(job, movedBy(good, {-0.01, 0})), "nesting.result.spacing"));
    EXPECT_TRUE(hasCode(validateNest(job, movedBy(good, {0, -1})), "nesting.result.outside_area"));
    NestResult bad = good;
    bad.sheets[0].placements[1].rotationDeg = 90; // stored outline no longer matches, 90 not allowed
    auto d = validateNest(job, bad);
    EXPECT_TRUE(hasCode(d, "nesting.result.rotation_not_allowed"));
    EXPECT_TRUE(hasCode(d, "nesting.result.outline_mismatch"));
    bad = good;
    bad.sheets[0].placements.pop_back();
    d = validateNest(job, bad);
    EXPECT_TRUE(hasCode(d, "nesting.result.quantity"));
    EXPECT_TRUE(hasCode(d, "nesting.result.area"));
    bad = good;
    bad.sheets.push_back(bad.sheets[0]);
    EXPECT_TRUE(hasCode(validateNest(job, bad), "nesting.result.stock"));
    bad = good;
    bad.isHeuristic = false;
    EXPECT_TRUE(hasCode(validateNest(job, bad), "nesting.result.not_heuristic"));
}

TEST(NestCancel, CancelledBeforeFirstLayoutIsAnError) {
    Job job = rectOnSheet(100, 100, RotationRule::None, 1000, 1000);
    NestOptions opt;
    opt.isCancelled = [] { return true; };
    const auto r = nest(job, opt);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, fos::core::ErrorCode::Cancelled);
}

TEST(NestCancel, CancelledLaterKeepsBestLayout) {
    Job job;
    job.sheets.push_back(makeSheet("S", 2000, 1000));
    job.parts.push_back(makePart("a", rectPoly(300, 200), 6, RotationRule::Quarter));
    job.parts.push_back(makePart("b", poly({{0, 0}, {400, 0}, {0, 300}}), 6, RotationRule::Quarter));
    NestOptions opt;
    opt.iterations = 1000;
    int calls = 0;
    opt.isCancelled = [&calls] { return ++calls > 400; };
    const NestResult r = nestValid(job, opt);
    EXPECT_TRUE(r.cancelled);
    EXPECT_LT(r.iterationsRun, 1000);
    EXPECT_EQ(r.placedCount, 12);
}
