// SPDX-License-Identifier: AGPL-3.0-only
// Quality benchmark: the improved search (all placement scores x four starting orders) must never use
// more sheets than the previous algorithm, reproduced by restricting the options to its single
// criterion (MinRightEdge) and its three orders. The legacy sheet counts measured with the code
// before this change (iterations = 5) are recorded as an additional fixed ceiling.
#include "benchmark_jobs.hpp"
#include "test_support.hpp"

#include <cstdio>
#include <map>

using namespace fos::nesting;
using namespace fos::nesting::test;

namespace {

NestOptions legacyOptions() {
    NestOptions o;
    o.iterations = 5;
    o.placementScores = {PlacementScore::MinRightEdge};
    o.orders = {InstanceOrder::AreaDesc, InstanceOrder::LongestSideDesc, InstanceOrder::BoxAreaDesc};
    return o;
}

// Sheets used by the pre-change nest() with iterations = 5 (measured: mixed60 3 @ 0.554,
// frames 3 @ 0.562, grain 2 @ 0.557, tri_trap 4 @ 0.504, hex_cross 2 @ 0.552).
const std::map<std::string, std::size_t> kLegacySheets{
    {"mixed60", 3}, {"frames", 3}, {"grain", 2}, {"tri_trap", 4}, {"hex_cross", 2}};

} // namespace

TEST(NestBenchmark, NewSearchNeverUsesMoreSheets) {
    for (const auto& b : benchmarkJobs()) {
        const NestResult old = nestValid(b.job, legacyOptions());
        NestOptions opt;
        opt.iterations = 5;
        const NestResult neu = nestValid(b.job, opt);
        EXPECT_TRUE(neu.unplaced.empty()) << b.name;
        EXPECT_LE(neu.sheets.size(), old.sheets.size()) << b.name;
        EXPECT_LE(neu.sheets.size(), kLegacySheets.at(b.name)) << b.name;
        EXPECT_GE(static_cast<int>(neu.sheets.size()), neu.sheetLowerBound) << b.name;
        std::printf("%-10s old %zu sheets util %.3f | new %zu sheets util %.3f (lower bound %d, %d decodes)\n",
                    b.name.c_str(), old.sheets.size(), old.utilisation, neu.sheets.size(), neu.utilisation,
                    neu.sheetLowerBound, neu.decodesRun);
        for (std::size_t i = 0; i < neu.sheets.size(); ++i)
            std::printf("    sheet %zu: utilisation %.3f (net %.3f)\n", i, neu.sheets[i].utilisation,
                        neu.sheets[i].netUtilisation);
    }
}

TEST(NestBenchmark, PartInPartSavesSheetsOnFrames) {
    const auto jobs = benchmarkJobs();
    const Job& job = jobs[1].job; // "frames"
    ASSERT_EQ(jobs[1].name, "frames");
    NestOptions opt;
    opt.iterations = 5;
    const NestResult plain = nestValid(job, opt);
    opt.partInPart = true;
    const NestResult pip = nestValid(job, opt);
    EXPECT_TRUE(pip.unplaced.empty());
    EXPECT_LT(pip.sheets.size(), plain.sheets.size());
    // Net area bound: frames 6 x (700*450 - 540*290) = 6 x 158 400 = 950 400; blocks 18 x 28 800 = 518 400;
    // octagon r = 60: 2 r^2 sqrt(2) = 10 182.3 -> x 12 = 122 188; total 1 590 988 mm^2. Placement area
    // (1500 - 28) x (1000 - 28) = 1 430 784 mm^2 -> ceil(1.112) = 2 sheets.
    EXPECT_EQ(pip.sheetLowerBound, 2);
    std::printf("frames part-in-part: %zu sheets (net util %.3f) vs %zu without\n", pip.sheets.size(),
                pip.netUtilisation, plain.sheets.size());
}
