// SPDX-License-Identifier: AGPL-3.0-only
// A kitchen-sized randomised job (fixed seed, own LCG -> identical on every platform).
#include "test_support.hpp"

#include "fos/cutting/svg.hpp"

#include <chrono>

using namespace fos::cutting;
using namespace fos::cutting::test;

namespace {

/// About 150 pieces with kitchen-like proportions on 2800 x 2070 boards, kerf 4, trim 10.
Job kitchenJob(std::uint32_t seed, Objective objective = Objective::MinimizeSheets) {
    TestLcg rng(seed);
    Job job = makeJob(2800, 2070, 4);
    job.id = "kitchen";
    job.objective = objective;
    job.trim = Trim{10, 10, 10, 10};
    int total = 0, n = 0;
    const auto add = [&](const char* kind, double l, double w, int q, bool rotate) {
        addPiece(job, std::string(kind) + "-" + std::to_string(++n), l, w, q, rotate);
        total += q;
    };
    for (int cabinet = 0; cabinet < 14; ++cabinet) {
        const double width = 300.0 + 50.0 * rng.range(0, 12);  // 300 .. 900 carcass width
        const double height = rng.range(0, 3) == 0 ? 2000.0 + 10.0 * rng.range(0, 20) : 720.0;
        const double depth = rng.range(0, 1) == 0 ? 560.0 : 320.0;
        add("side", height, depth, 2, false);
        add("horizontal", width - 36.0, depth, 2, false);
        add("shelf", width - 37.0, depth - 20.0, rng.range(1, 4), false);
        add("rail", width - 36.0, 100.0, 2, true);
        add("door", height - 4.0, width / 2.0 - 3.0, 2, false);
    }
    while (total < 150) add("filler", 100.0 + 10.0 * rng.range(0, 60), 60.0 + 10.0 * rng.range(0, 30), 1, true);
    return job;
}

int totalQuantity(const Job& job) {
    int q = 0;
    for (const Piece& p : job.pieces) q += p.quantity;
    return q;
}

} // namespace

TEST(LargeJob, KitchenJobIsValidDenseAndDeterministic) {
    const Job job = kitchenJob(12345u);
    const int required = totalQuantity(job);
    ASSERT_GE(required, 100);
    ASSERT_LE(required, 200);

    const auto t0 = std::chrono::steady_clock::now();
    const CuttingResult r = solve(job);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    expectValid(job, r);
    EXPECT_TRUE(r.unplaced.empty());
    EXPECT_EQ(r.totals.placedCount, required);
    EXPECT_FALSE(r.isProvenOptimal);
    RecordProperty("sheets", static_cast<int>(r.sheets.size()));
    RecordProperty("lowerBound", r.sheetCountLowerBound);
    RecordProperty("milliseconds", static_cast<int>(ms));
    std::cout << "kitchen job: " << required << " pieces, " << r.sheets.size() << " sheets (area bound "
              << r.sheetCountLowerBound << "), utilisation " << r.totals.utilisationOfSheetPct << " % of sheet, "
              << r.totals.cutCount << " cuts, " << ms << " ms, " << r.strategy << "\n";

    // Sanity thresholds for a heuristic (not optimality claims): at most one sheet above the
    // area bound, and every sheet but the emptiest at least 80 % full.
    EXPECT_GE(static_cast<int>(r.sheets.size()), r.sheetCountLowerBound);
    EXPECT_LE(static_cast<int>(r.sheets.size()), r.sheetCountLowerBound + 1);
    double used = 0, least = 1e300;
    for (const SheetLayout& s : r.sheets) {
        used += s.usedArea;
        least = std::min(least, s.usedArea);
    }
    ASSERT_GT(r.sheets.size(), 1u);
    const double sheetArea = 2800.0 * 2070.0;
    const double utilisationWithoutEmptiest =
        100.0 * (used - least) / (sheetArea * static_cast<double>(r.sheets.size() - 1));
    EXPECT_GT(utilisationWithoutEmptiest, 80.0);
    EXPECT_GT(r.totals.utilisationOfSheetPct, 70.0);

    // Determinism on the big job: identical drawing of every sheet.
    const CuttingResult again = solve(job);
    ASSERT_EQ(again.sheets.size(), r.sheets.size());
    for (std::size_t i = 0; i < r.sheets.size(); ++i)
        EXPECT_EQ(renderSheetSvg(job, r.sheets[i]), renderSheetSvg(job, again.sheets[i]));
    EXPECT_EQ(again.strategy, r.strategy);
}

// The improvement phase may only ever improve the selected objective key.
TEST(LargeJob, ImprovementPhaseNeverMakesTheResultWorse) {
    const Job job = kitchenJob(777u);
    Options off;
    off.improvementRuns = 0;
    const CuttingResult base = solve(job, off);
    const CuttingResult improved = solve(job);
    expectValid(job, base);
    expectValid(job, improved);
    EXPECT_EQ(base.improvementRuns, 0);
    EXPECT_GT(improved.improvementRuns, 0);
    EXPECT_LE(improved.improvementRuns, Options{}.improvementRuns);
    const auto lost = [](const CuttingResult& r) {
        return r.totals.sheetArea - r.totals.usedArea - r.totals.remnantArea;
    };
    // MinimizeSheets key: (sheets, lost area, ...).
    ASSERT_LE(improved.sheets.size(), base.sheets.size());
    if (improved.sheets.size() == base.sheets.size()) {
        EXPECT_LE(lost(improved), lost(base) + 1e-6);
    }
}

// Several seeds and all objectives through the validator, including limited stages.
TEST(LargeJob, ManySeedsAndObjectivesStayValid) {
    Options quick;
    quick.improvementRuns = 20;
    for (std::uint32_t seed : {1u, 2u, 3u}) {
        for (Objective o : {Objective::MinimizeSheets, Objective::MinimizeWaste, Objective::PreferRemnants,
                            Objective::MinimizeCuts}) {
            Job job = kitchenJob(seed, o);
            if (seed == 2u) job.maxStages = 3;
            if (seed == 3u) job.firstCut = FirstCut::Lengthwise;
            if (o == Objective::PreferRemnants) {
                StockSheet remnant;
                remnant.id = "remnant";
                remnant.length = 1500;
                remnant.width = 900;
                remnant.quantity = 2;
                remnant.isRemnant = true;
                job.stock.push_back(remnant);
            }
            const CuttingResult r = solve(job, quick);
            expectValid(job, r);
            EXPECT_TRUE(r.unplaced.empty()) << "seed " << seed << " objective " << toString(o);
            if (o == Objective::PreferRemnants) {
                EXPECT_EQ(r.stockUsage[1].sheetsUsed, 2);
            }
            if (seed == 2u) {
                for (const SheetLayout& s : r.sheets) EXPECT_LE(s.stageCount, 3);
            }
        }
    }
}

// A time budget stops the search early but still returns a complete, valid layout.
TEST(LargeJob, TimeBudgetStopsEarlyWithValidLayout) {
    const Job job = kitchenJob(99u);
    Options o;
    o.timeBudgetMs = 1;
    const CuttingResult r = solve(job, o);
    expectValid(job, r);
    EXPECT_GE(r.strategiesTried, 1);
    EXPECT_TRUE(r.unplaced.empty());
    EXPECT_FALSE(r.cancelled);
}
