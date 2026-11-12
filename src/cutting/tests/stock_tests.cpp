// SPDX-License-Identifier: AGPL-3.0-only
// Stock selection: remnants, limits, priorities.
#include "test_support.hpp"

using namespace fos::cutting;
using namespace fos::cutting::test;

namespace {

/// Full boards 2800 x 2070 (unlimited) plus one listed remnant 1200 x 800.
Job jobWithRemnant(Objective objective) {
    Job job = makeJob(2800, 2070, 4);
    job.objective = objective;
    StockSheet remnant;
    remnant.id = "remnant-1";
    remnant.length = 1200;
    remnant.width = 800;
    remnant.quantity = 1;
    remnant.isRemnant = true;
    job.stock.push_back(remnant);
    return job;
}

} // namespace

// Two pieces 500 x 700 fit the remnant side by side: 500 + 4 + 500 = 1004 <= 1200, 700 <= 800.
// PreferRemnants must therefore use the remnant and no full board.
TEST(Remnants, PreferRemnantsUsesTheRemnantInsteadOfAFullSheet) {
    Job job = jobWithRemnant(Objective::PreferRemnants);
    addPiece(job, "p", 500, 700, 2, false);
    const CuttingResult r = solve(job);
    expectValid(job, r);
    ASSERT_EQ(r.sheets.size(), 1u);
    EXPECT_EQ(r.sheets[0].stockId, "remnant-1");
    ASSERT_EQ(r.stockUsage.size(), 2u);
    EXPECT_EQ(r.stockUsage[0].stockId, "board");
    EXPECT_EQ(r.stockUsage[0].sheetsUsed, 0);
    EXPECT_EQ(r.stockUsage[1].stockId, "remnant-1");
    EXPECT_EQ(r.stockUsage[1].sheetsUsed, 1);
}

// Four such pieces: the remnant holds at most two (a third in the row needs 1508 > 1200, a
// second row needs 700 + 4 + 700 > 800), so one full board is unavoidable. PreferRemnants
// still cuts two from the remnant (remnant stock first) and two from the board.
TEST(Remnants, PreferRemnantsFillsTheRemnantBeforeOpeningAFullSheet) {
    Job job = jobWithRemnant(Objective::PreferRemnants);
    addPiece(job, "p", 500, 700, 4, false);
    const CuttingResult r = solve(job);
    expectValid(job, r);
    ASSERT_EQ(r.sheets.size(), 2u);
    EXPECT_EQ(r.stockUsage[0].sheetsUsed, 1);
    EXPECT_EQ(r.stockUsage[1].sheetsUsed, 1);
    for (const SheetLayout& s : r.sheets) EXPECT_EQ(s.placements.size(), 2u);
    EXPECT_TRUE(r.unplaced.empty());
}

// MinimizeSheets with the same four pieces: everything fits one full board, which beats
// remnant + board (2 sheets).
TEST(Remnants, MinimizeSheetsIgnoresRemnantWhenItCostsAnExtraSheet) {
    Job job = jobWithRemnant(Objective::MinimizeSheets);
    addPiece(job, "p", 500, 700, 4, false);
    const CuttingResult r = solve(job);
    expectValid(job, r);
    ASSERT_EQ(r.sheets.size(), 1u);
    EXPECT_EQ(r.sheets[0].stockId, "board");
    // Bound: the largest usable area 2800 * 2070 covers 1400000 mm2 -> 1 sheet -> proven minimal.
    EXPECT_TRUE(r.isSheetCountProvenMinimal);
}

// Stock limit: 2 sheets of 2000 x 1000, kerf 0, pieces 1000 x 1000 x 5. Two per sheet ->
// 4 placed, 1 left over with reason StockExhausted.
TEST(StockLimits, QuantityIsNeverExceeded) {
    Job job = makeJob(2000, 1000, 0);
    job.stock[0].quantity = 2;
    addPiece(job, "sq", 1000, 1000, 5);
    const CuttingResult r = solve(job);
    expectValid(job, r);
    EXPECT_EQ(r.sheets.size(), 2u);
    EXPECT_EQ(r.totals.placedCount, 4);
    ASSERT_EQ(r.unplaced.size(), 1u);
    EXPECT_EQ(r.unplaced[0].quantity, 1);
    EXPECT_EQ(r.unplaced[0].reason, UnplacedReason::StockExhausted);
    EXPECT_EQ(r.unplaced[0].message,
              "1 of 5 required copies of piece 'sq' 1000 \xC3\x97 1000 could not be placed: the 2 available "
              "stock sheet(s) large enough for it are used up.");
    EXPECT_EQ(r.stockUsage[0].sheetsUsed, 2);
    EXPECT_DOUBLE_EQ(r.totals.utilisationOfSheetPct, 100.0);
}

// A stock type with quantity 0 is never used; the other type takes everything.
TEST(StockLimits, ZeroQuantityStockIsIgnored) {
    Job job = makeJob(3000, 2000, 4);
    job.stock[0].quantity = 0;
    StockSheet small;
    small.id = "small";
    small.length = 1000;
    small.width = 600;
    job.stock.push_back(small);
    addPiece(job, "p", 900, 500, 2);
    addPiece(job, "wide", 1500, 500); // fits only the 3000 x 2000 type, which has no sheets
    const CuttingResult r = solve(job);
    expectValid(job, r);
    EXPECT_EQ(r.stockUsage[0].sheetsUsed, 0);
    EXPECT_EQ(r.stockUsage[1].sheetsUsed, 2); // 900 + 4 + 900 > 1000 -> one per sheet
    ASSERT_EQ(r.unplaced.size(), 1u);
    EXPECT_EQ(r.unplaced[0].pieceId, "wide");
    EXPECT_EQ(r.unplaced[0].reason, UnplacedReason::StockExhausted);
}

// Two stock types of identical size: the one with the higher priority is opened first.
TEST(StockPriority, HigherPriorityStockIsUsedFirst) {
    Job job = makeJob(2000, 1000, 4);
    StockSheet second = job.stock[0];
    second.id = "preferred";
    second.priority = 5;
    job.stock.push_back(second);
    addPiece(job, "p", 800, 600, 2);
    const CuttingResult r = solve(job);
    expectValid(job, r);
    ASSERT_EQ(r.sheets.size(), 1u);
    EXPECT_EQ(r.sheets[0].stockId, "preferred");
}

// Cost is summed over the used sheets: 2 boards at 60 = 120.
TEST(StockCost, TotalsSumTheSheetCosts) {
    Job job = makeJob(2000, 1000, 0);
    job.stock[0].cost = 60.0;
    addPiece(job, "sq", 1000, 1000, 4);
    const CuttingResult r = solve(job);
    expectValid(job, r);
    EXPECT_EQ(r.sheets.size(), 2u);
    EXPECT_DOUBLE_EQ(r.totals.stockCost, 120.0);
}

// Remnant classification: one piece 1500 x 800 in the corner of a 2000 x 1000 sheet, kerf 4,
// minimum remnant 300 x 100. Beyond the kerf 496 mm remain along X and 196 mm along Y.
//  * crosswise first:  right strip 496 x 1000 = 496000, top strip 1500 x 196 = 294000 -> 790000;
//  * lengthwise first: top strip 2000 x 196 = 392000, right piece 496 x 800 = 396800 -> 788800.
// All four rectangles are reusable, so the re-split step must end on the crosswise-first
// variant (790000 mm2). Its kerf is 4*1000 + 4*1500 = 10000; 1200000 + 790000 + 10000 = 2000000.
TEST(RemnantsClassification, OffcutsAreClassifiedAndTheBetterSplitIsKept) {
    Job job = makeJob(2000, 1000, 4);
    addPiece(job, "p", 1500, 800, 1, false);
    const CuttingResult r = solve(job);
    expectValid(job, r);
    ASSERT_EQ(r.sheets.size(), 1u);
    const SheetLayout& s = r.sheets[0];
    ASSERT_EQ(s.offcuts.size(), 2u);
    EXPECT_TRUE(s.offcuts[0].reusable);
    EXPECT_TRUE(s.offcuts[1].reusable);
    EXPECT_NEAR(s.remnantArea, 790000.0, 1e-6);
    EXPECT_NEAR(s.kerfArea, 10000.0, 1e-6);
    EXPECT_NEAR(s.wasteArea, 0.0, 1e-9);
    EXPECT_NEAR(s.usedArea + s.kerfArea + s.remnantArea + s.wasteArea, 2000000.0, 1e-6);

    // With a minimum remnant of 300 x 200 the 196 mm strips are waste in both variants;
    // the reusable part is the 496 x 1000 strip (496000) vs 496 x 800 (396800) -> 496000.
    job.minRemnantWidth = 200;
    const CuttingResult r2 = solve(job);
    expectValid(job, r2);
    EXPECT_NEAR(r2.sheets[0].remnantArea, 496000.0, 1e-6);
    EXPECT_NEAR(r2.sheets[0].wasteArea, 1500.0 * 196.0, 1e-6);
}
