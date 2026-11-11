// SPDX-License-Identifier: AGPL-3.0-only
// Hand-reasoned cases for exact kerf and trim handling.
#include "test_support.hpp"

using namespace fos::cutting;
using namespace fos::cutting::test;

// 4 pieces 1000 x 500 on a 2000 x 1000 sheet, kerf 0: 2 x 2 grid, 4 * 500000 = 2000000 mm2
// = the whole sheet. One sheet, 100 %, three cuts (one through the sheet, one per half).
TEST(Kerf, FourQuartersWithoutKerfFillOneSheetCompletely) {
    Job job = makeJob(2000, 1000, 0);
    addPiece(job, "q", 1000, 500, 4);
    const CuttingResult r = solve(job);
    expectValid(job, r);
    ASSERT_EQ(r.sheets.size(), 1u);
    EXPECT_TRUE(r.unplaced.empty());
    EXPECT_EQ(r.sheets[0].placements.size(), 4u);
    EXPECT_DOUBLE_EQ(r.sheets[0].utilisationOfSheetPct, 100.0);
    EXPECT_DOUBLE_EQ(r.sheets[0].utilisationOfUsablePct, 100.0);
    EXPECT_DOUBLE_EQ(r.totals.utilisationOfSheetPct, 100.0);
    EXPECT_EQ(r.sheets[0].cuts.size(), 3u);
    EXPECT_TRUE(r.sheets[0].offcuts.empty());
    EXPECT_DOUBLE_EQ(r.sheets[0].kerfArea, 0.0);
    // Area bound: 2000000 / 2000000 = 1 sheet, and 1 sheet is used -> count proven minimal.
    EXPECT_EQ(r.sheetCountLowerBound, 1);
    EXPECT_TRUE(r.isSheetCountProvenMinimal);
    EXPECT_FALSE(r.isProvenOptimal);
}

// Same with kerf 4. The four pieces alone have the area of the whole sheet, so any kerf
// makes one sheet impossible. How many fit on one sheet?
//  * unrotated (1000 along X): two side by side need 1000 + 4 + 1000 = 2004 > 2000, two
//    stacked need 500 + 4 + 500 = 1004 > 1000 -> only one unrotated piece per column/row;
//  * rotated (500 along X, 1000 along Y): 500 + 4 + 500 + 4 + 500 = 1508 <= 2000, a fourth
//    would need 2012 > 2000 -> three per sheet.
// So the best is 3 + 1 pieces on two sheets. The area bound stays 1, so nothing is proven.
TEST(Kerf, FourQuartersWithKerfNeedSecondSheet) {
    Job job = makeJob(2000, 1000, 4);
    addPiece(job, "q", 1000, 500, 4);
    const CuttingResult r = solve(job);
    expectValid(job, r);
    ASSERT_EQ(r.sheets.size(), 2u);
    EXPECT_TRUE(r.unplaced.empty());
    EXPECT_EQ(r.sheets[0].placements.size() + r.sheets[1].placements.size(), 4u);
    EXPECT_EQ(std::max(r.sheets[0].placements.size(), r.sheets[1].placements.size()), 3u);
    EXPECT_EQ(r.sheetCountLowerBound, 1);
    EXPECT_FALSE(r.isSheetCountProvenMinimal);
    EXPECT_FALSE(r.isProvenOptimal);
}

// With a single sheet in stock the fourth piece must be reported, not squeezed in.
TEST(Kerf, FourQuartersWithKerfAndOneSheetLeaveOneUnplaced) {
    Job job = makeJob(2000, 1000, 4);
    job.stock[0].quantity = 1;
    addPiece(job, "q", 1000, 500, 4);
    const CuttingResult r = solve(job);
    expectValid(job, r);
    ASSERT_EQ(r.sheets.size(), 1u);
    EXPECT_EQ(r.sheets[0].placements.size(), 3u);
    for (const Placement& p : r.sheets[0].placements) EXPECT_TRUE(p.rotated);
    ASSERT_EQ(r.unplaced.size(), 1u);
    EXPECT_EQ(r.unplaced[0].pieceId, "q");
    EXPECT_EQ(r.unplaced[0].quantity, 1);
    EXPECT_EQ(r.unplaced[0].reason, UnplacedReason::StockExhausted);
    EXPECT_NE(r.unplaced[0].message.find("1 of 4"), std::string::npos) << r.unplaced[0].message;
    EXPECT_FALSE(r.isSheetCountProvenMinimal);
}

// Grain-locked variant: only the unrotated orientation is legal, and (see above) only one
// unrotated piece fits per sheet -> four sheets.
TEST(Kerf, FourQuartersWithKerfAndGrainNeedFourSheets) {
    Job job = makeJob(2000, 1000, 4);
    addPiece(job, "q", 1000, 500, 4, false);
    const CuttingResult r = solve(job);
    expectValid(job, r);
    EXPECT_EQ(r.sheets.size(), 4u);
    for (const SheetLayout& s : r.sheets) {
        ASSERT_EQ(s.placements.size(), 1u);
        EXPECT_FALSE(s.placements[0].rotated);
    }
}

// Exact fit with kerf: 998 + 4 + 998 = 2000 along X and 498 + 4 + 498 = 1000 along Y.
// One sheet, no offcuts. Kerf: one cut through the whole sheet plus one cut in each half:
// either 4*1000 + 2 * 4*998 or 4*2000 + 2 * 4*498, both = 11984 mm2.
// Pieces: 4 * 998 * 498 = 1988016 mm2; 1988016 + 11984 = 2000000. Utilisation 99.4008 %.
TEST(Kerf, ExactFitWithKerfInBothDirections) {
    Job job = makeJob(2000, 1000, 4);
    addPiece(job, "p", 998, 498, 4);
    const CuttingResult r = solve(job);
    expectValid(job, r);
    ASSERT_EQ(r.sheets.size(), 1u);
    const SheetLayout& s = r.sheets[0];
    EXPECT_EQ(s.placements.size(), 4u);
    EXPECT_TRUE(s.offcuts.empty());
    EXPECT_EQ(s.cuts.size(), 3u);
    EXPECT_NEAR(s.kerfArea, 11984.0, 1e-6);
    EXPECT_NEAR(s.usedArea, 1988016.0, 1e-6);
    EXPECT_NEAR(s.utilisationOfSheetPct, 99.4008, 1e-9);
    EXPECT_TRUE(r.isSheetCountProvenMinimal);
}

// 998 x 500 x 4 with kerf 4 on 2000 x 1000: 998 + 4 + 998 = 2000 fits along X, but two rows
// need 500 + 4 + 500 = 1004 > 1000. Rotated, three fit (500 + 4 + 500 + 4 + 500 = 1508) and
// the remaining 488 mm strip takes nothing. So one sheet holds at most three -> two sheets.
TEST(Kerf, ExactFitAlongLengthOnlyStillNeedsTwoSheets) {
    Job job = makeJob(2000, 1000, 4);
    addPiece(job, "p", 998, 500, 4);
    const CuttingResult r = solve(job);
    expectValid(job, r);
    EXPECT_EQ(r.sheets.size(), 2u);
    EXPECT_TRUE(r.unplaced.empty());

    // On a 2000 x 500 strip two of them fit exactly: one cut, 4 * 500 = 2000 mm2 of kerf.
    Job strip = makeJob(2000, 500, 4);
    addPiece(strip, "p", 998, 500, 2, false);
    const CuttingResult rs = solve(strip);
    expectValid(strip, rs);
    ASSERT_EQ(rs.sheets.size(), 1u);
    ASSERT_EQ(rs.sheets[0].cuts.size(), 1u);
    EXPECT_EQ(rs.sheets[0].cuts[0].orientation, CutOrientation::Crosswise);
    EXPECT_DOUBLE_EQ(rs.sheets[0].cuts[0].position, 998.0);
    EXPECT_DOUBLE_EQ(rs.sheets[0].cuts[0].kerfWidth, 4.0);
    EXPECT_DOUBLE_EQ(rs.sheets[0].cuts[0].extent, 500.0);
    EXPECT_NEAR(rs.sheets[0].kerfArea, 2000.0, 1e-9);
    EXPECT_TRUE(rs.sheets[0].offcuts.empty());
}

// No phantom kerf: after 600 + kerf 4 the remaining strip is exactly 396 mm, so a 396 mm
// piece fits without a further kerf; a 397 mm piece does not (600 + 4 + 397 = 1001).
TEST(Kerf, PieceExactlyAsLargeAsRemainingStripFits) {
    Job fits = makeJob(1000, 500, 4);
    addPiece(fits, "a", 600, 500, 1, false);
    addPiece(fits, "b", 396, 500, 1, false);
    const CuttingResult r = solve(fits);
    expectValid(fits, r);
    ASSERT_EQ(r.sheets.size(), 1u);
    EXPECT_EQ(r.sheets[0].placements.size(), 2u);
    EXPECT_EQ(r.sheets[0].cuts.size(), 1u);

    Job tooLong = makeJob(1000, 500, 4);
    addPiece(tooLong, "a", 600, 500, 1, false);
    addPiece(tooLong, "b", 397, 500, 1, false);
    const CuttingResult r2 = solve(tooLong);
    expectValid(tooLong, r2);
    EXPECT_EQ(r2.sheets.size(), 2u);
}

// A piece 998 long on a 1000 long sheet leaves 2 mm, less than the 4 mm blade: the cut is
// still needed, but it only removes those 2 mm (kerf width clipped, 2 * 500 = 1000 mm2) and
// leaves no offcut. 499000 + 1000 = 500000.
TEST(Kerf, KerfIsClippedWhenLessThanOneKerfRemains) {
    Job job = makeJob(1000, 500, 4);
    addPiece(job, "a", 998, 500, 1, false);
    const CuttingResult r = solve(job);
    expectValid(job, r);
    ASSERT_EQ(r.sheets.size(), 1u);
    const SheetLayout& s = r.sheets[0];
    ASSERT_EQ(s.cuts.size(), 1u);
    EXPECT_DOUBLE_EQ(s.cuts[0].position, 998.0);
    EXPECT_DOUBLE_EQ(s.cuts[0].kerfWidth, 2.0);
    EXPECT_TRUE(s.offcuts.empty());
    EXPECT_NEAR(s.kerfArea, 1000.0, 1e-9);
    EXPECT_NEAR(s.usedArea, 499000.0, 1e-9);
}

// A piece flush with the usable-area boundary needs no kerf there: with 10 mm trim all
// round a 2020 x 1020 sheet has a usable area of exactly 2000 x 1000 at (10, 10), which the
// four quarters fill completely with kerf 0.
// trimArea = 2020*1020 - 2000*1000 = 2060400 - 2000000 = 60400 mm2.
// Utilisation: 100 % of the usable area, 2000000 / 2060400 = 97.0685... % of the sheet.
TEST(Trim, SymmetricTrimDefinesUsableArea) {
    Job job = makeJob(2020, 1020, 0);
    job.trim = Trim{10, 10, 10, 10};
    addPiece(job, "q", 1000, 500, 4);
    const CuttingResult r = solve(job);
    expectValid(job, r);
    ASSERT_EQ(r.sheets.size(), 1u);
    const SheetLayout& s = r.sheets[0];
    EXPECT_DOUBLE_EQ(s.usable.x, 10);
    EXPECT_DOUBLE_EQ(s.usable.y, 10);
    EXPECT_DOUBLE_EQ(s.usable.length, 2000);
    EXPECT_DOUBLE_EQ(s.usable.width, 1000);
    EXPECT_NEAR(s.trimArea, 60400.0, 1e-6);
    EXPECT_NEAR(s.utilisationOfUsablePct, 100.0, 1e-9);
    EXPECT_NEAR(s.utilisationOfSheetPct, 100.0 * 2000000.0 / 2060400.0, 1e-9);
    for (const Placement& p : s.placements) {
        EXPECT_GE(p.x, 10.0);
        EXPECT_GE(p.y, 10.0);
        EXPECT_LE(p.x + p.length, 2010.0);
        EXPECT_LE(p.y + p.width, 1010.0);
    }
    // 4 trim cuts first (left, right, bottom, top), then the 3 split cuts.
    ASSERT_EQ(s.cuts.size(), 7u);
    for (std::size_t i = 0; i < 4; ++i) EXPECT_EQ(s.cuts[i].kind, CutKind::Trim);
    for (std::size_t i = 4; i < 7; ++i) EXPECT_EQ(s.cuts[i].kind, CutKind::Split);
    EXPECT_DOUBLE_EQ(s.cuts[0].position, 10.0);
    EXPECT_DOUBLE_EQ(s.cuts[1].position, 2010.0);
    EXPECT_DOUBLE_EQ(s.cuts[0].extent, 1020.0);  // full sheet width
    EXPECT_DOUBLE_EQ(s.cuts[2].extent, 2000.0);  // after both end trims
    EXPECT_DOUBLE_EQ(s.cuts[3].position, 1010.0);
}

// Asymmetric trim: left 15, right 5, top 20, bottom 0 on a 1020 x 520 sheet leaves
// x in [15, 1015], y in [0, 500], i.e. 1000 x 500. A 1000 x 500 piece fits exactly at
// (15, 0); a 1001 x 500 piece fits nowhere (rotated: 500 x 1001 > 500 wide either).
TEST(Trim, AsymmetricTrimAndOversizeMessage) {
    Job job = makeJob(1020, 520, 4);
    job.trim.left = 15;
    job.trim.right = 5;
    job.trim.top = 20;
    job.trim.bottom = 0;
    addPiece(job, "exact", 1000, 500);
    addPiece(job, "big", 1001, 500);
    const CuttingResult r = solve(job);
    expectValid(job, r);
    ASSERT_EQ(r.sheets.size(), 1u);
    ASSERT_EQ(r.sheets[0].placements.size(), 1u);
    EXPECT_EQ(r.sheets[0].placements[0].pieceId, "exact");
    EXPECT_DOUBLE_EQ(r.sheets[0].placements[0].x, 15.0);
    EXPECT_DOUBLE_EQ(r.sheets[0].placements[0].y, 0.0);
    EXPECT_EQ(r.sheets[0].cuts.size(), 3u); // left, right, top trim; no bottom trim, no split
    ASSERT_EQ(r.unplaced.size(), 1u);
    EXPECT_EQ(r.unplaced[0].reason, UnplacedReason::DoesNotFitAnyStock);
    EXPECT_EQ(r.unplaced[0].message,
              "Piece 'big' 1001 \xC3\x97 500 does not fit the usable area 1000 \xC3\x97 500 of any stock sheet.");
}
