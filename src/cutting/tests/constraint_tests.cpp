// SPDX-License-Identifier: AGPL-3.0-only
// Grain, stages, first-cut preference, objectives, input checks, cancellation, determinism.
#include "test_support.hpp"

#include "fos/cutting/svg.hpp"

#include <limits>

using namespace fos::cutting;
using namespace fos::cutting::test;

// Sheet 1400 long x 2000 wide. A grain-locked piece with length 1500 must lie with its
// length along the sheet length: 1500 > 1400 -> impossible. Turned by 90 degrees it would
// need 600 <= 1400 along X and 1500 <= 2000 along Y -> fits, but only if rotation is allowed.
TEST(Grain, LockedPieceLongerThanSheetLengthCannotBePlaced) {
    Job job = makeJob(1400, 2000, 4);
    addPiece(job, "panel", 1500, 600, 1, false);
    const CuttingResult r = solve(job);
    expectValid(job, r);
    EXPECT_TRUE(r.sheets.empty());
    ASSERT_EQ(r.unplaced.size(), 1u);
    EXPECT_EQ(r.unplaced[0].reason, UnplacedReason::DoesNotFitAnyStock);
    EXPECT_EQ(r.unplaced[0].message,
              "Piece 'panel' 1500 \xC3\x97 600 does not fit the usable area 1400 \xC3\x97 2000 of any stock sheet; "
              "it would fit turned by 90\xC2\xB0, but rotation is not allowed for this piece (grain).");
    EXPECT_EQ(r.totals.unplacedCount, 1);
}

TEST(Grain, RotatablePieceIsTurnedToFit) {
    Job job = makeJob(1400, 2000, 4);
    addPiece(job, "panel", 1500, 600, 1, true);
    const CuttingResult r = solve(job);
    expectValid(job, r);
    ASSERT_EQ(r.sheets.size(), 1u);
    ASSERT_EQ(r.sheets[0].placements.size(), 1u);
    const Placement& p = r.sheets[0].placements[0];
    EXPECT_TRUE(p.rotated);
    EXPECT_DOUBLE_EQ(p.length, 600.0);  // extent along the sheet length
    EXPECT_DOUBLE_EQ(p.width, 1500.0);
    EXPECT_TRUE(r.unplaced.empty());
}

// The same numbers the other way round (length 600, width 1500, locked) do fit: the piece
// length 600 runs along the 1400 sheet length and its width 1500 across the 2000 width.
TEST(Grain, LockedPieceFitsWhenItsLengthRunsAlongTheSheet) {
    Job job = makeJob(1400, 2000, 4);
    addPiece(job, "panel", 600, 1500, 2, false);
    const CuttingResult r = solve(job);
    expectValid(job, r);
    // Two side by side: 600 + 4 + 600 = 1204 <= 1400 -> one sheet.
    ASSERT_EQ(r.sheets.size(), 1u);
    for (const Placement& p : r.sheets[0].placements) {
        EXPECT_FALSE(p.rotated);
        EXPECT_DOUBLE_EQ(p.length, 600.0);
    }
}

// 'Tall side' 2900 x 600 on 2800 x 2070 sheets with 10 mm trim: usable 2780 x 2050.
// 2900 > 2780 and (rotated) 2900 > 2050 -> reported; the other piece is still cut.
TEST(Oversize, PieceIsReportedWithSizesAndOthersArePlaced) {
    Job job = makeJob(2800, 2070, 4);
    job.trim = Trim{10, 10, 10, 10};
    addPiece(job, "tall", 2900, 600);
    job.pieces.back().label = "Tall side";
    addPiece(job, "shelf", 800, 500, 3);
    const CuttingResult r = solve(job);
    expectValid(job, r);
    ASSERT_EQ(r.unplaced.size(), 1u);
    EXPECT_EQ(r.unplaced[0].pieceId, "tall");
    EXPECT_EQ(r.unplaced[0].message,
              "Piece 'Tall side' 2900 \xC3\x97 600 does not fit the usable area 2780 \xC3\x97 2050 of any stock sheet.");
    EXPECT_EQ(placedCount(r, "shelf"), 3);
    EXPECT_EQ(r.sheets.size(), 1u);
    EXPECT_FALSE(r.isSheetCountProvenMinimal); // something is unplaced
}

Job mixedJob() {
    Job job = makeJob(2800, 2070, 4);
    job.trim = Trim{10, 10, 10, 10};
    addPiece(job, "side", 720, 560, 6, false);
    addPiece(job, "shelf", 764, 540, 8, false);
    addPiece(job, "door", 716, 397, 10);
    addPiece(job, "rail", 764, 100, 9);
    addPiece(job, "plinth", 1200, 150, 3);
    return job;
}

TEST(Stages, TwoStageLimitIsRespected) {
    Job limited = mixedJob();
    limited.maxStages = 2;
    const CuttingResult r2 = solve(limited);
    expectValid(limited, r2); // the validator recomputes the stages from the cut sequence
    for (const SheetLayout& s : r2.sheets) EXPECT_LE(s.stageCount, 2);
    for (const SheetLayout& s : r2.sheets)
        for (const Cut& c : s.cuts) EXPECT_LE(c.stage, 2);

    Job free = mixedJob();
    const CuttingResult rf = solve(free);
    expectValid(free, rf);
    // A restriction can never help: the unrestricted run needs at most as many sheets.
    EXPECT_LE(rf.sheets.size(), r2.sheets.size());
    EXPECT_TRUE(rf.unplaced.empty());
}

TEST(Stages, ThreeStageLimitIsRespected) {
    Job job = mixedJob();
    job.maxStages = 3;
    const CuttingResult r = solve(job);
    expectValid(job, r);
    for (const SheetLayout& s : r.sheets) EXPECT_LE(s.stageCount, 3);
    EXPECT_TRUE(r.unplaced.empty());
}

// One piece 700 x 400 in the corner of a 2000 x 1000 sheet needs one cut in each direction,
// so the direction of the first cut is a free choice and the preference must decide it.
TEST(FirstCut, PreferenceDecidesTheFirstSplitCut) {
    for (FirstCut pref : {FirstCut::Lengthwise, FirstCut::Crosswise}) {
        Job job = makeJob(2000, 1000, 4);
        job.firstCut = pref;
        addPiece(job, "a", 700, 400, 1, false);
        addPiece(job, "b", 500, 300, 2, false);
        const CuttingResult r = solve(job);
        expectValid(job, r);
        ASSERT_EQ(r.sheets.size(), 1u);
        ASSERT_FALSE(r.sheets[0].cuts.empty());
        const Cut& first = r.sheets[0].cuts[0];
        EXPECT_EQ(first.kind, CutKind::Split);
        EXPECT_EQ(first.stage, 1);
        EXPECT_EQ(first.orientation,
                  pref == FirstCut::Lengthwise ? CutOrientation::Lengthwise : CutOrientation::Crosswise);
        // Edge to edge through the whole sheet.
        EXPECT_DOUBLE_EQ(first.extent, pref == FirstCut::Lengthwise ? 2000.0 : 1000.0);
    }
}

// With the improvement phase off, every objective compares the same set of candidate
// layouts, so the winners must be ordered consistently with the documented keys.
TEST(Objective, WinnersAreConsistentWithTheirKeys) {
    Options noImprove;
    noImprove.improvementRuns = 0;
    Job sheets = mixedJob();
    sheets.objective = Objective::MinimizeSheets;
    Job cuts = mixedJob();
    cuts.objective = Objective::MinimizeCuts;
    Job waste = mixedJob();
    waste.objective = Objective::MinimizeWaste;
    const CuttingResult rs = solve(sheets, noImprove);
    const CuttingResult rc = solve(cuts, noImprove);
    const CuttingResult rw = solve(waste, noImprove);
    expectValid(sheets, rs);
    expectValid(cuts, rc);
    expectValid(waste, rw);
    const auto lost = [](const CuttingResult& r) {
        return r.totals.sheetArea - r.totals.usedArea - r.totals.remnantArea;
    };
    // MinimizeCuts = (sheets, cuts, ...): same sheet count as MinimizeSheets, never more cuts.
    EXPECT_EQ(rc.sheets.size(), rs.sheets.size());
    EXPECT_LE(rc.totals.cutCount, rs.totals.cutCount);
    // MinimizeSheets = (sheets, lost area, ...): never more lost area than the fewest-cuts layout.
    EXPECT_LE(lost(rs), lost(rc) + 1e-6);
    // MinimizeWaste = (lost area, ...): never more lost area than any other winner.
    EXPECT_LE(lost(rw), lost(rs) + 1e-6);
    EXPECT_EQ(rs.improvementRuns, 0);
    EXPECT_GT(rs.strategiesTried, 50);
}

TEST(Input, InvalidJobsAreRejectedWithSpecificMessages) {
    Job negativeKerf = makeJob(2000, 1000, -1);
    addPiece(negativeKerf, "a", 100, 100);
    auto r1 = optimize(negativeKerf);
    ASSERT_FALSE(r1.ok());
    EXPECT_EQ(r1.error().code, fos::core::ErrorCode::InvalidArgument);
    EXPECT_NE(r1.error().message.find("kerf"), std::string::npos);
    EXPECT_TRUE(hasCode(validateJob(negativeKerf), "cutting.job.invalid_kerf"));

    Job dup = makeJob(2000, 1000, 4);
    addPiece(dup, "a", 100, 100);
    addPiece(dup, "a", 200, 100);
    EXPECT_TRUE(hasCode(validateJob(dup), "cutting.job.piece_id_duplicate"));
    EXPECT_FALSE(optimize(dup).ok());

    Job zero = makeJob(2000, 1000, 4);
    addPiece(zero, "a", 0, 100);
    addPiece(zero, "b", 100, 100, 0);
    const auto ds = validateJob(zero);
    EXPECT_TRUE(hasCode(ds, "cutting.job.piece_size_invalid"));
    EXPECT_TRUE(hasCode(ds, "cutting.job.piece_quantity_invalid"));
    EXPECT_FALSE(optimize(zero).ok());

    Job noStock = makeJob(2000, 1000, 4);
    noStock.stock.clear();
    addPiece(noStock, "a", 100, 100);
    EXPECT_TRUE(hasCode(validateJob(noStock), "cutting.job.no_stock"));
    EXPECT_FALSE(optimize(noStock).ok());

    // Trim 600 + 600 on a 1000 wide sheet leaves nothing.
    Job allTrim = makeJob(2000, 1000, 4);
    allTrim.trim.top = 600;
    allTrim.trim.bottom = 600;
    addPiece(allTrim, "a", 100, 100);
    EXPECT_TRUE(hasCode(validateJob(allTrim), "cutting.job.stock_no_usable_area"));
    EXPECT_FALSE(optimize(allTrim).ok());

    Job oneStage = makeJob(2000, 1000, 4);
    oneStage.maxStages = 1;
    EXPECT_TRUE(hasCode(validateJob(oneStage), "cutting.job.invalid_max_stages"));

    Job nan = makeJob(2000, 1000, 4);
    addPiece(nan, "a", std::numeric_limits<double>::quiet_NaN(), 100);
    EXPECT_FALSE(optimize(nan).ok());

    Job huge = makeJob(2000, 1000, 4);
    addPiece(huge, "a", 10, 10, 2000000000);
    EXPECT_TRUE(hasCode(validateJob(huge), "cutting.job.too_many_pieces"));
    EXPECT_FALSE(optimize(huge).ok());
}

TEST(Input, EmptyJobGivesEmptyValidResult) {
    Job job = makeJob(2000, 1000, 4);
    const CuttingResult r = solve(job);
    expectValid(job, r);
    EXPECT_TRUE(r.sheets.empty());
    EXPECT_EQ(r.sheetCountLowerBound, 0);
    EXPECT_TRUE(r.isSheetCountProvenMinimal); // zero sheets for zero pieces
    EXPECT_DOUBLE_EQ(r.totals.utilisationOfSheetPct, 0.0);
}

TEST(Cancellation, CancelBeforeFirstLayoutIsAnError) {
    Job job = mixedJob();
    Options o;
    o.isCancelled = [] { return true; };
    auto r = optimize(job, o);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, fos::core::ErrorCode::Cancelled);
}

TEST(Cancellation, CancelLaterReturnsBestLayoutSoFar) {
    Job job = mixedJob();
    int polls = 0;
    Options o;
    o.isCancelled = [&polls] { return ++polls > 5; }; // allow exactly 5 packing runs
    const CuttingResult r = solve(job, o);
    expectValid(job, r);
    EXPECT_TRUE(r.cancelled);
    EXPECT_EQ(r.strategiesTried, 5);
    EXPECT_EQ(r.improvementRuns, 0);
    EXPECT_TRUE(r.unplaced.empty());
    EXPECT_EQ(r.totals.placedCount, 36);

    const CuttingResult full = solve(job);
    EXPECT_FALSE(full.cancelled);
    EXPECT_GT(full.strategiesTried, 5);
    EXPECT_LE(full.sheets.size(), r.sheets.size());
}

TEST(Determinism, SameInputTwiceGivesIdenticalOutput) {
    const Job job = mixedJob();
    const CuttingResult a = solve(job);
    const CuttingResult b = solve(job);
    expectValid(job, a);
    EXPECT_EQ(a.strategy, b.strategy);
    ASSERT_EQ(a.sheets.size(), b.sheets.size());
    for (std::size_t i = 0; i < a.sheets.size(); ++i) {
        // The SVG contains every placement, kerf strip and offcut with 0.001 mm resolution.
        EXPECT_EQ(renderSheetSvg(job, a.sheets[i]), renderSheetSvg(job, b.sheets[i]));
        ASSERT_EQ(a.sheets[i].cuts.size(), b.sheets[i].cuts.size());
        for (std::size_t c = 0; c < a.sheets[i].cuts.size(); ++c) {
            EXPECT_EQ(a.sheets[i].cuts[c].position, b.sheets[i].cuts[c].position);
            EXPECT_EQ(a.sheets[i].cuts[c].orientation, b.sheets[i].cuts[c].orientation);
        }
        ASSERT_EQ(a.sheets[i].placements.size(), b.sheets[i].placements.size());
        for (std::size_t p = 0; p < a.sheets[i].placements.size(); ++p) {
            EXPECT_EQ(a.sheets[i].placements[p].pieceId, b.sheets[i].placements[p].pieceId);
            EXPECT_EQ(a.sheets[i].placements[p].instance, b.sheets[i].placements[p].instance);
            EXPECT_EQ(a.sheets[i].placements[p].x, b.sheets[i].placements[p].x);
            EXPECT_EQ(a.sheets[i].placements[p].y, b.sheets[i].placements[p].y);
        }
    }
    EXPECT_EQ(a.totals.wasteArea, b.totals.wasteArea);
}

// Area-based lower bound on the sheet count, by hand.
TEST(LowerBound, AreaBoundArithmetic) {
    Job job = makeJob(2000, 1000, 4); // usable 2000000 mm2 per sheet, unlimited
    EXPECT_EQ(areaLowerBoundOnSheetCount(job, 0.0), 0);
    EXPECT_EQ(areaLowerBoundOnSheetCount(job, 1.0), 1);
    EXPECT_EQ(areaLowerBoundOnSheetCount(job, 2000000.0), 1);
    EXPECT_EQ(areaLowerBoundOnSheetCount(job, 2000001.0), 2);
    EXPECT_EQ(areaLowerBoundOnSheetCount(job, 6000000.0), 3);

    // Trim 10 all round: usable 1980 * 980 = 1940400 mm2 -> 2000000 needs 2 sheets.
    job.trim = Trim{10, 10, 10, 10};
    EXPECT_EQ(areaLowerBoundOnSheetCount(job, 1940400.0), 1);
    EXPECT_EQ(areaLowerBoundOnSheetCount(job, 2000000.0), 2);
    job.trim = Trim{};

    // Limited stock: 2 sheets = 4000000 mm2 at most.
    job.stock[0].quantity = 2;
    EXPECT_EQ(areaLowerBoundOnSheetCount(job, 4000000.0), 2);
    EXPECT_EQ(areaLowerBoundOnSheetCount(job, 4000001.0), -1);

    // Plus one remnant 1000 x 1000 (1000000 mm2): largest first -> 2 + 1 sheets cover 5000000.
    StockSheet remnant;
    remnant.id = "remnant";
    remnant.length = 1000;
    remnant.width = 1000;
    remnant.quantity = 1;
    remnant.isRemnant = true;
    job.stock.push_back(remnant);
    EXPECT_EQ(areaLowerBoundOnSheetCount(job, 4500000.0), 3);
    EXPECT_EQ(areaLowerBoundOnSheetCount(job, 3900000.0), 2);
    EXPECT_EQ(areaLowerBoundOnSheetCount(job, 5100000.0), -1);
}

// 5 pieces 1000 x 990 on 2000 x 1000 sheets, kerf 4: area 4950000 mm2 -> bound
// ceil(4950000 / 2000000) = 3. Unrotated only one fits per sheet (1000 + 4 + 1000 = 2004 > 2000),
// but turned by 90 degrees two fit: 990 + 4 + 990 = 1984 <= 2000 along X, 1000 <= 1000 along Y.
// So 5 pieces need 3 sheets (2 + 2 + 1) = the bound: the sheet count is proven minimal.
TEST(LowerBound, FlagIsSetOnlyWhenBoundIsReached) {
    Job job = makeJob(2000, 1000, 4);
    addPiece(job, "p", 1000, 990, 5);
    const CuttingResult r = solve(job);
    expectValid(job, r);
    EXPECT_EQ(r.sheets.size(), 3u);
    EXPECT_EQ(r.sheetCountLowerBound, 3);
    EXPECT_TRUE(r.isSheetCountProvenMinimal);
    EXPECT_FALSE(r.isProvenOptimal);

    // Grain-locked, only one fits per sheet: 5 sheets against a bound of 3 -> not proven,
    // although 5 is in fact the true minimum (the area bound is weak, and we say so).
    Job locked = makeJob(2000, 1000, 4);
    addPiece(locked, "p", 1000, 990, 5, false);
    const CuttingResult rl = solve(locked);
    expectValid(locked, rl);
    EXPECT_EQ(rl.sheets.size(), 5u);
    EXPECT_EQ(rl.sheetCountLowerBound, 3);
    EXPECT_FALSE(rl.isSheetCountProvenMinimal);
}
