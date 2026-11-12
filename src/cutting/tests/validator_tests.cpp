// SPDX-License-Identifier: AGPL-3.0-only
// The validator must catch corrupted layouts: every test starts from a valid result,
// breaks exactly one thing and expects the matching finding.
#include "test_support.hpp"

using namespace fos::cutting;
using namespace fos::cutting::test;
using fos::core::Diagnostics;

namespace {

/// 2000 x 1000 board with 10 mm trim, kerf 4: 'a' 900 x 400 x 3 (grain locked) and 'b' 500 x 300 x 2.
Job baseJob() {
    Job job = makeJob(2000, 1000, 4);
    job.trim = Trim{10, 10, 10, 10};
    addPiece(job, "a", 900, 400, 3, false);
    addPiece(job, "b", 500, 300, 2, true);
    return job;
}

struct Fixture {
    Job job = baseJob();
    CuttingResult result = solve(job);
};

} // namespace

TEST(Validator, AcceptsTheUntouchedResult) {
    Fixture f;
    EXPECT_TRUE(validateLayout(f.job, f.result).empty());
    ASSERT_EQ(f.result.sheets.size(), 1u);
    EXPECT_EQ(f.result.sheets[0].placements.size(), 5u);
}

TEST(Validator, DetectsPlacementOutsideUsableArea) {
    Fixture f;
    f.result.sheets[0].placements[0].x = 5.0; // inside the 10 mm trim
    const Diagnostics ds = validateLayout(f.job, f.result);
    EXPECT_TRUE(hasCode(ds, "cutting.layout.outside_usable_area"));
    EXPECT_TRUE(hasCode(ds, "cutting.layout.placement_not_cut"));
}

TEST(Validator, DetectsOverlapAndMissingKerfGap) {
    Fixture f;
    auto& pl = f.result.sheets[0].placements;
    // Put the second placement 2 mm (less than the 4 mm kerf) beside the first one.
    pl[1].x = pl[0].x + pl[0].length + 2.0;
    pl[1].y = pl[0].y;
    EXPECT_TRUE(hasCode(validateLayout(f.job, f.result), "cutting.layout.overlap_or_kerf"));
    // And fully on top of it.
    pl[1].x = pl[0].x;
    EXPECT_TRUE(hasCode(validateLayout(f.job, f.result), "cutting.layout.overlap_or_kerf"));
}

TEST(Validator, DetectsIllegalRotationAndWrongSize) {
    Fixture f;
    for (Placement& p : f.result.sheets[0].placements)
        if (p.pieceId == "a") {
            p.rotated = true; // 'a' is grain locked
            break;
        }
    const Diagnostics ds = validateLayout(f.job, f.result);
    EXPECT_TRUE(hasCode(ds, "cutting.layout.illegal_rotation"));
    EXPECT_TRUE(hasCode(ds, "cutting.layout.wrong_size")); // flag no longer matches the placed size

    Fixture g;
    g.result.sheets[0].placements[0].length -= 1.0;
    EXPECT_TRUE(hasCode(validateLayout(g.job, g.result), "cutting.layout.wrong_size"));
}

TEST(Validator, DetectsDuplicateMissingAndUnknownPieces) {
    Fixture f;
    auto& pl = f.result.sheets[0].placements;
    std::size_t first = pl.size(), second = pl.size();
    for (std::size_t i = 0; i < pl.size(); ++i)
        if (pl[i].pieceId == "a") (first == pl.size() ? first : second) = i;
    ASSERT_LT(second, pl.size());
    pl[second].instance = pl[first].instance;
    const Diagnostics ds = validateLayout(f.job, f.result);
    EXPECT_TRUE(hasCode(ds, "cutting.layout.duplicate_instance"));
    EXPECT_TRUE(hasCode(ds, "cutting.layout.quantity_mismatch"));

    Fixture g;
    g.result.sheets[0].placements[0].pieceId = "ghost";
    EXPECT_TRUE(hasCode(validateLayout(g.job, g.result), "cutting.layout.unknown_piece"));

    Fixture h;
    h.result.sheets[0].placements[0].instance = 7;
    EXPECT_TRUE(hasCode(validateLayout(h.job, h.result), "cutting.layout.instance_out_of_range"));

    // Job asks for one more than the layout delivers and nothing is reported unplaced.
    Fixture k;
    k.job.pieces[0].quantity = 4;
    EXPECT_TRUE(hasCode(validateLayout(k.job, k.result), "cutting.layout.quantity_mismatch"));
}

TEST(Validator, DetectsBrokenCutSequence) {
    {
        Fixture f; // drop the last cut: two pieces of material stay joined
        f.result.sheets[0].cuts.pop_back();
        const Diagnostics ds = validateLayout(f.job, f.result);
        EXPECT_TRUE(hasCode(ds, "cutting.layout.unaccounted_material"));
    }
    {
        Fixture f; // a cut that does not run edge to edge
        Cut& c = f.result.sheets[0].cuts.back();
        c.extent -= 50.0;
        EXPECT_TRUE(hasCode(validateLayout(f.job, f.result), "cutting.layout.cut_not_edge_to_edge"));
    }
    {
        Fixture f; // a cut through material that does not exist
        Cut& c = f.result.sheets[0].cuts.back();
        c.source.x += 30.0;
        EXPECT_TRUE(hasCode(validateLayout(f.job, f.result), "cutting.layout.cut_source_missing"));
    }
    {
        Fixture f; // wrong kerf width
        for (Cut& c : f.result.sheets[0].cuts)
            if (c.kind == CutKind::Split) {
                c.kerfWidth = 1.0;
                break;
            }
        EXPECT_TRUE(hasCode(validateLayout(f.job, f.result), "cutting.layout.kerf_width"));
    }
    {
        Fixture f; // moved cut: the pieces it should produce no longer come out
        for (Cut& c : f.result.sheets[0].cuts)
            if (c.kind == CutKind::Split) {
                c.position += 7.0;
                break;
            }
        const Diagnostics ds = validateLayout(f.job, f.result);
        EXPECT_TRUE(fos::core::hasErrors(ds));
    }
    {
        Fixture f; // wrong stage number
        for (Cut& c : f.result.sheets[0].cuts)
            if (c.kind == CutKind::Split) {
                c.stage += 1;
                break;
            }
        EXPECT_TRUE(hasCode(validateLayout(f.job, f.result), "cutting.layout.cut_stage"));
    }
    {
        Fixture f; // trim cut through the usable area
        ASSERT_EQ(f.result.sheets[0].cuts[0].kind, CutKind::Trim);
        f.result.sheets[0].cuts[0].position = 500.0;
        EXPECT_TRUE(hasCode(validateLayout(f.job, f.result), "cutting.layout.trim_cut_in_usable_area"));
    }
}

// The base layout needs three cutting stages; checked against a two-stage job it must fail.
TEST(Validator, DetectsStageLimitViolation) {
    Fixture f;
    ASSERT_EQ(f.result.sheets[0].stageCount, 3);
    f.job.maxStages = 3;
    EXPECT_TRUE(validateLayout(f.job, f.result).empty());
    f.job.maxStages = 2;
    EXPECT_TRUE(hasCode(validateLayout(f.job, f.result), "cutting.layout.too_many_stages"));
}

TEST(Validator, DetectsWrongAreasOffcutsAndTotals) {
    {
        Fixture f;
        f.result.sheets[0].kerfArea += 100.0;
        EXPECT_TRUE(hasCode(validateLayout(f.job, f.result), "cutting.layout.area_mismatch"));
    }
    {
        Fixture f;
        f.result.sheets[0].utilisationOfSheetPct += 1.0;
        EXPECT_TRUE(hasCode(validateLayout(f.job, f.result), "cutting.layout.utilisation"));
    }
    {
        Fixture f;
        ASSERT_FALSE(f.result.sheets[0].offcuts.empty());
        f.result.sheets[0].offcuts[0].reusable = !f.result.sheets[0].offcuts[0].reusable;
        EXPECT_TRUE(hasCode(validateLayout(f.job, f.result), "cutting.layout.offcut_classification"));
    }
    {
        Fixture f;
        f.result.sheets[0].offcuts.pop_back();
        EXPECT_TRUE(hasCode(validateLayout(f.job, f.result), "cutting.layout.unaccounted_material"));
    }
    {
        Fixture f;
        f.result.totals.usedArea += 1000.0;
        EXPECT_TRUE(hasCode(validateLayout(f.job, f.result), "cutting.layout.totals_mismatch"));
    }
    {
        Fixture f;
        f.result.totals.cutCount += 1;
        EXPECT_TRUE(hasCode(validateLayout(f.job, f.result), "cutting.layout.totals_mismatch"));
    }
    {
        Fixture f; // tree no longer describes the cut sequence
        for (CutNode& n : f.result.sheets[0].tree)
            if (n.kind == NodeKind::Cut) {
                n.position += 3.0;
                break;
            }
        EXPECT_TRUE(hasCode(validateLayout(f.job, f.result), "cutting.layout.tree_mismatch"));
    }
}

TEST(Validator, DetectsStockProblemsAndFalseClaims) {
    {
        Fixture f; // the job has no sheet of this type available
        f.job.stock[0].quantity = 0;
        EXPECT_TRUE(hasCode(validateLayout(f.job, f.result), "cutting.layout.stock_exceeded"));
    }
    {
        Fixture f;
        f.result.sheets[0].stockId = "other";
        EXPECT_TRUE(hasCode(validateLayout(f.job, f.result), "cutting.layout.unknown_stock"));
    }
    {
        Fixture f;
        f.result.sheets[0].sheetLength = 2100.0;
        EXPECT_TRUE(hasCode(validateLayout(f.job, f.result), "cutting.layout.stock_size"));
    }
    {
        Fixture f;
        f.result.stockUsage[0].sheetsUsed = 3;
        EXPECT_TRUE(hasCode(validateLayout(f.job, f.result), "cutting.layout.stock_usage"));
    }
    {
        Fixture f;
        f.result.isProvenOptimal = true;
        EXPECT_TRUE(hasCode(validateLayout(f.job, f.result), "cutting.layout.optimality_claim"));
    }
    {
        Fixture f;
        f.result.sheetCountLowerBound = 0;
        EXPECT_TRUE(hasCode(validateLayout(f.job, f.result), "cutting.layout.lower_bound"));
    }
    {
        Fixture f;
        f.result.isSheetCountProvenMinimal = !f.result.isSheetCountProvenMinimal;
        EXPECT_TRUE(hasCode(validateLayout(f.job, f.result), "cutting.layout.minimal_flag"));
    }
}

// The validator also works on a layout built by hand, without the optimiser: one piece
// 600 x 400 at the corner of a 1000 x 500 sheet, kerf 4, crosswise cut first.
//   cut 1: crosswise at x = 600 through the sheet (extent 500) -> right offcut 396 x 500
//   cut 2: lengthwise at y = 400 through the 600 x 500 part (extent 600) -> top offcut 600 x 96
// kerf = 4*500 + 4*600 = 4400; offcuts 198000 (reusable) + 57600 (96 < 100 -> waste);
// 240000 + 4400 + 198000 + 57600 = 500000.
TEST(Validator, AcceptsHandBuiltLayout) {
    Job job = makeJob(1000, 500, 4);
    addPiece(job, "p", 600, 400);

    SheetLayout s;
    s.stockId = "board";
    s.stockIndex = 0;
    s.sheetLength = 1000;
    s.sheetWidth = 500;
    s.usable = Rect{0, 0, 1000, 500};
    s.placements.push_back({"p", 0, 0, 0, 600, 400, false});
    s.offcuts.push_back({Rect{604, 0, 396, 500}, true});
    s.offcuts.push_back({Rect{0, 404, 600, 96}, false});
    Cut c1;
    c1.orientation = CutOrientation::Crosswise;
    c1.position = 600;
    c1.kerfWidth = 4;
    c1.extent = 500;
    c1.source = Rect{0, 0, 1000, 500};
    c1.stage = 1;
    c1.node = 0;
    Cut c2;
    c2.orientation = CutOrientation::Lengthwise;
    c2.position = 400;
    c2.kerfWidth = 4;
    c2.extent = 600;
    c2.source = Rect{0, 0, 600, 500};
    c2.stage = 2;
    c2.node = 1;
    s.cuts = {c1, c2};
    CutNode root, left, right, piece, top;
    root.kind = NodeKind::Cut; root.rect = c1.source; root.orientation = c1.orientation; root.position = 600;
    root.kerfWidth = 4; root.stage = 1; root.low = 1; root.high = 2;
    left.kind = NodeKind::Cut; left.rect = c2.source; left.parent = 0; left.orientation = c2.orientation;
    left.position = 400; left.kerfWidth = 4; left.stage = 2; left.low = 3; left.high = 4;
    right.kind = NodeKind::Offcut; right.rect = s.offcuts[0].rect; right.parent = 0; right.item = 0;
    piece.kind = NodeKind::Piece; piece.rect = Rect{0, 0, 600, 400}; piece.parent = 1; piece.item = 0;
    top.kind = NodeKind::Offcut; top.rect = s.offcuts[1].rect; top.parent = 1; top.item = 1;
    s.tree = {root, left, right, piece, top};
    s.sheetArea = 500000;
    s.usableArea = 500000;
    s.trimArea = 0;
    s.usedArea = 240000;
    s.kerfArea = 4400;
    s.remnantArea = 198000;
    s.wasteArea = 57600;
    s.utilisationOfSheetPct = 48.0;
    s.utilisationOfUsablePct = 48.0;
    s.cutLength = 1100;
    s.stageCount = 2;

    CuttingResult r;
    r.jobId = job.id;
    r.sheets.push_back(s);
    r.stockUsage.push_back({"board", 1});
    r.totals.sheetCount = 1;
    r.totals.placedCount = 1;
    r.totals.sheetArea = r.totals.usableArea = 500000;
    r.totals.usedArea = 240000;
    r.totals.kerfArea = 4400;
    r.totals.remnantArea = 198000;
    r.totals.wasteArea = 57600;
    r.totals.utilisationOfSheetPct = r.totals.utilisationOfUsablePct = 48.0;
    r.totals.cutCount = 2;
    r.totals.cutLength = 1100;
    r.sheetCountLowerBound = 1;
    r.isSheetCountProvenMinimal = true;
    const Diagnostics ds = validateLayout(job, r);
    for (const auto& d : ds) ADD_FAILURE() << d.code << ": " << d.message;

    // The optimiser must find a layout with the same accounting for this trivial job
    // (it prefers the split with more reusable area, which is this one: lengthwise first
    // would give 1000 x 96 waste = 96000 and only 396 x 400 = 158400 reusable).
    const CuttingResult o = solve(job);
    expectValid(job, o);
    EXPECT_NEAR(o.sheets[0].remnantArea, 198000.0, 1e-6);
    EXPECT_NEAR(o.sheets[0].wasteArea, 57600.0, 1e-6);
    EXPECT_NEAR(o.sheets[0].kerfArea, 4400.0, 1e-6);

    // Non-guillotine tampering of the hand-built layout: shorten the second cut's source so
    // it would stop in the middle of the material.
    r.sheets[0].cuts[1].source = Rect{0, 0, 300, 500};
    EXPECT_TRUE(hasCode(validateLayout(job, r), "cutting.layout.cut_source_missing"));
}
