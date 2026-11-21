// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/linear/linear.hpp"
#include "fos/linear/svg.hpp"

#include <gtest/gtest.h>

namespace fos::linear {
namespace {

LinearJob bars6000(std::vector<Piece> pieces, double kerf = 0) {
    LinearJob j;
    j.pieces = std::move(pieces);
    j.stock = {Stock{"bar6000", 6000, 0, true, 10.0, false, 0}};
    j.kerf = kerf;
    return j;
}

LinearResult run(const LinearJob& j) {
    auto r = optimizeLinear(j);
    EXPECT_TRUE(r.ok()) << (r.ok() ? "" : r.error().message);
    EXPECT_TRUE(validateLinearPlan(j, r.value()).empty());
    return r.value();
}

TEST(Linear, ExactFitWithKerfUsesOneBar) {
    // 1998 + 4 + 1998 + 4 + 1996 = 6000: fills the bar exactly, no kerf at the bar end.
    const auto j = bars6000({{"A", "", 1998, 2}, {"B", "", 1996, 1}}, 4);
    const auto r = run(j);
    ASSERT_EQ(r.bars.size(), 1u);
    const auto& b = r.bars[0];
    ASSERT_EQ(b.cuts.size(), 3u);
    EXPECT_DOUBLE_EQ(b.cuts[0].start, 0);
    EXPECT_DOUBLE_EQ(b.cuts[1].start, 2002); // 1998 + 4
    EXPECT_DOUBLE_EQ(b.cuts[2].start, 4004); // 2002 + 1998 + 4
    EXPECT_EQ(b.cuts[2].pieceId, "B");
    EXPECT_EQ(b.kerfCount, 2);
    EXPECT_DOUBLE_EQ(b.kerfLoss, 8);
    EXPECT_DOUBLE_EQ(b.offcutLength, 0);
    EXPECT_DOUBLE_EQ(b.utilisation, 5992.0 / 6000.0);
    // LB = ceil((2002 + 2002 + 2000) / (6000 + 4)) = ceil(6004 / 6004) = 1.
    EXPECT_EQ(r.barCountLowerBound, 1);
    EXPECT_TRUE(r.isBarCountProvenMinimal);
}

TEST(Linear, KerfMakesThreeEqualPiecesNeedTwoBars) {
    // 3 * 1998 + 2 * 4 = 6002 > 6000. LB = ceil(3 * 2002 / 6004) = ceil(1.0003) = 2.
    const auto r = run(bars6000({{"A", "", 1998, 3}}, 4));
    EXPECT_EQ(r.bars.size(), 2u);
    EXPECT_EQ(r.barCountLowerBound, 2);
    EXPECT_TRUE(r.isBarCountProvenMinimal);
    EXPECT_DOUBLE_EQ(r.totalCost, 20.0);
}

TEST(Linear, TrimsAndReusableOffcut) {
    auto j = bars6000({{"A", "", 2990, 2}});
    j.trimStart = j.trimEnd = 10; // usable 5980 = 2 * 2990 exactly
    auto r = run(j);
    ASSERT_EQ(r.bars.size(), 1u);
    EXPECT_DOUBLE_EQ(r.bars[0].cuts[0].start, 10);
    EXPECT_DOUBLE_EQ(r.bars[0].cuts[1].start, 3000);
    j.pieces[0].length = 2991; // 5982 > 5980
    EXPECT_EQ(run(j).bars.size(), 2u);

    auto k = bars6000({{"P", "", 1000, 1}}, 4);
    k.trimStart = k.trimEnd = 10;
    k.minReusableOffcut = 500;
    r = run(k);
    // Usable end 5990, piece ends at 1010, separating kerf 4 -> offcut 5990 - 1014 = 4976 at 1014.
    const auto& b = r.bars[0];
    EXPECT_DOUBLE_EQ(b.offcutStart, 1014);
    EXPECT_DOUBLE_EQ(b.offcutLength, 4976);
    EXPECT_TRUE(b.offcutReusable);
    EXPECT_EQ(b.kerfCount, 1);
    EXPECT_DOUBLE_EQ(r.totalWaste, 24); // 6000 - 1000 - 4976 = 20 trim + 4 kerf
}

TEST(Linear, UnplacedReasons) {
    auto r = run(bars6000({{"long", "", 7000, 1}, {"ok", "", 100, 1}}));
    ASSERT_EQ(r.unplaced.size(), 1u);
    EXPECT_EQ(r.unplaced[0].reason, UnplacedReason::LongerThanEveryStock);
    EXPECT_NE(r.unplaced[0].message.find("7000 mm"), std::string::npos);
    EXPECT_FALSE(r.isBarCountProvenMinimal);

    LinearJob j = bars6000({{"P", "", 4000, 2}});
    j.stock[0].unlimited = false;
    j.stock[0].quantity = 1;
    r = run(j);
    EXPECT_EQ(r.placedCount, 1);
    ASSERT_EQ(r.unplaced.size(), 1u);
    EXPECT_EQ(r.unplaced[0].reason, UnplacedReason::StockExhausted);
}

TEST(Linear, LowerBoundNotReachedIsNotClaimedMinimal) {
    // 3 x 3100 on 6000: LB = ceil(9300 / 6000) = 2 but two 3100 never share a bar -> 3 bars.
    const auto r = run(bars6000({{"A", "", 3100, 3}}));
    EXPECT_EQ(r.bars.size(), 3u);
    EXPECT_EQ(r.barCountLowerBound, 2);
    EXPECT_FALSE(r.isBarCountProvenMinimal);
}

TEST(Linear, ShorterStockAndOffcutsPreferred) {
    LinearJob j = bars6000({{"A", "", 2400, 1}});
    j.stock.push_back(Stock{"rest2500", 2500, 1, false, 0, true, 0});
    j.objective = Objective::MinimizeWaste;
    auto r = run(j);
    ASSERT_EQ(r.bars.size(), 1u);
    EXPECT_EQ(r.bars[0].stockId, "rest2500");
    j.objective = Objective::PreferOffcuts;
    EXPECT_EQ(run(j).bars[0].stockId, "rest2500");
}

TEST(Linear, ValidatorCatchesCorruptedPlans) {
    const auto j = bars6000({{"A", "", 1998, 2}, {"B", "", 1996, 1}}, 4);
    const auto good = run(j);
    auto bad = good;
    bad.bars[0].cuts[1].start -= 1; // kerf gap 3 < 4
    auto d = validateLinearPlan(j, bad);
    ASSERT_FALSE(d.empty());
    EXPECT_EQ(d[0].code, "linear.validate.overlap");
    bad = good;
    bad.bars[0].cuts[2].pieceId = "A"; // instance 0 of A twice, B missing
    bad.bars[0].cuts[2].instance = 0;
    d = validateLinearPlan(j, bad);
    bool dup = false, missing = false;
    for (const auto& x : d) {
        dup |= x.code == "linear.validate.duplicate_instance";
        missing |= x.code == "linear.validate.missing_instance";
    }
    EXPECT_TRUE(dup && missing);
    bad = good;
    bad.bars[0].kerfLoss = 4;
    EXPECT_FALSE(validateLinearPlan(j, bad).empty());
    bad = good;
    bad.bars.push_back(good.bars[0]);
    EXPECT_FALSE(validateLinearPlan(j, bad).empty());
}

TEST(Linear, DeterministicAndValidOnMixedJob) {
    LinearJob j;
    for (int i = 0; i < 20; ++i)
        j.pieces.push_back(Piece{"p" + std::to_string(i), "", 300.0 + 137.0 * ((i * 7) % 13), 1 + i % 3});
    j.stock = {Stock{"s6000", 6000, 0, true, 0, false, 0}, Stock{"s3000", 3000, 2, false, 0, false, 0}};
    j.kerf = 3.2;
    j.trimStart = 5;
    j.trimEnd = 5;
    const auto a = run(j), b = run(j);
    ASSERT_EQ(a.bars.size(), b.bars.size());
    for (std::size_t i = 0; i < a.bars.size(); ++i) {
        ASSERT_EQ(a.bars[i].cuts.size(), b.bars[i].cuts.size());
        for (std::size_t c = 0; c < a.bars[i].cuts.size(); ++c) {
            EXPECT_EQ(a.bars[i].cuts[c].pieceId, b.bars[i].cuts[c].pieceId);
            EXPECT_EQ(a.bars[i].cuts[c].start, b.bars[i].cuts[c].start);
        }
    }
    EXPECT_EQ(renderLinearSvg(j, a), renderLinearSvg(j, b));
    EXPECT_GE(static_cast<int>(a.bars.size()), a.barCountLowerBound);
    EXPECT_NE(renderLinearSvg(j, a).find("viewBox=\"0 0 6000 "), std::string::npos);
}

TEST(Linear, InvalidJobsAreRefused) {
    EXPECT_FALSE(optimizeLinear(bars6000({{"A", "", -1, 1}})).ok());
    EXPECT_FALSE(optimizeLinear(bars6000({{"A", "", 10, 0}})).ok());
    EXPECT_FALSE(optimizeLinear(bars6000({{"A", "", 10, 1}, {"A", "", 20, 1}})).ok());
    auto j = bars6000({{"A", "", 10, 1}});
    j.trimStart = 3000;
    j.trimEnd = 3000;
    auto r = optimizeLinear(j);
    ASSERT_FALSE(r.ok());
    EXPECT_NE(r.error().message.find("bar6000"), std::string::npos);
    EXPECT_FALSE(optimizeLinear(LinearJob{}).ok());
}

} // namespace
} // namespace fos::linear
