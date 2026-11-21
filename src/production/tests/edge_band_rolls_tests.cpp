// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/domain/fixtures.hpp"
#include "fos/linear/linear.hpp"
#include "fos/production/edge_band_rolls.hpp"

#include <gtest/gtest.h>

using namespace fos;
using namespace fos::production;

TEST(EdgeBandRolls, BasicCabinetNeedsOneRollPerBand) {
    const auto model = domain::fixtures::basicCabinet();
    const auto jobs = edgeBandStripJobs(model); // overhang 25 mm per end, 100 m rolls
    ASSERT_EQ(jobs.size(), 2u);
    const auto& abs2 = jobs[0];
    EXPECT_EQ(abs2.edgeBandId, domain::EdgeBandId("eb.abs2"));
    EXPECT_EQ(abs2.stripCount, 5);
    EXPECT_NEAR(abs2.appliedLength, 3732.0, 1e-9); // 2 * 720 + 3 * 764
    double pieces = 0;
    for (const auto& p : abs2.job.pieces) pieces += p.length;
    EXPECT_NEAR(pieces, 3982.0, 1e-9); // 3732 + 5 strips * 2 * 25
    EXPECT_DOUBLE_EQ(abs2.job.stock[0].cost, 80.0); // 0.80 per m * 100 m

    auto r = linear::optimizeLinear(abs2.job);
    ASSERT_TRUE(r.ok());
    EXPECT_TRUE(linear::validateLinearPlan(abs2.job, r.value()).empty());
    ASSERT_EQ(r.value().bars.size(), 1u);
    EXPECT_TRUE(r.value().isBarCountProvenMinimal);
    EXPECT_NEAR(r.value().utilisation, 3982.0 / 100000.0, 1e-12);
    EXPECT_EQ(jobs[1].stripCount, 4); // abs04: 4 * 560
    EXPECT_NEAR(jobs[1].appliedLength, 2240.0, 1e-9);
}

TEST(EdgeBandRolls, ShortRollsSplitStripsAndBadRollLengthIsEmpty) {
    const auto model = domain::fixtures::basicCabinet();
    EdgeBandRollOptions o;
    o.rollLength = 1600; // abs2 strips 770, 770, 814, 814, 814: two per roll at most (770+770=1540)
    const auto jobs = edgeBandStripJobs(model, o);
    auto r = linear::optimizeLinear(jobs[0].job);
    ASSERT_TRUE(r.ok());
    // LB = ceil(3982 / 1600) = 3; 770+770 | 814 | 814 | 814? 814+770 = 1584 fits -> 3 rolls.
    EXPECT_EQ(r.value().bars.size(), 3u);
    EXPECT_TRUE(linear::validateLinearPlan(jobs[0].job, r.value()).empty());
    o.rollLength = 0;
    EXPECT_TRUE(edgeBandStripJobs(model, o).empty());
}
