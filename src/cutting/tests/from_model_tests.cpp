// SPDX-License-Identifier: AGPL-3.0-only
#include "test_support.hpp"

#include "fos/cutting/from_model.hpp"
#include "fos/domain/fixtures.hpp"

using namespace fos::cutting;
using namespace fos::cutting::test;
namespace domain = fos::domain;

// basicCabinet(): see fos/domain/fixtures.hpp for the hand-written cut sizes.
//  mat.pb18 (grain): sides 719.2 x 558 x 2, bottom + top 764 x 558 x 2, shelf 764 x 538 x 1,
//                    all grain-locked (parts have grain AlongLength, the material has grain);
//  mat.hdf3 (no grain): back 796 x 716 x 1, free to rotate.
TEST(FromModel, BasicCabinetGivesOneJobPerMaterialWithCutSizes) {
    const domain::Model model = domain::fixtures::basicCabinet();
    fos::core::Diagnostics ds;
    const Settings settings;
    const std::vector<Job> jobs = jobsFromModel(model, settings, ds);
    EXPECT_TRUE(ds.empty());
    ASSERT_EQ(jobs.size(), 2u);

    const Job& pb = jobs[0];
    EXPECT_EQ(pb.id, "mat.pb18");
    EXPECT_EQ(pb.name, "Particleboard 18 mm");
    EXPECT_DOUBLE_EQ(pb.thickness, 18.0);
    EXPECT_DOUBLE_EQ(pb.kerf, 4.0);
    ASSERT_EQ(pb.stock.size(), 1u);
    EXPECT_EQ(pb.stock[0].id, "mat.pb18/sheet");
    EXPECT_DOUBLE_EQ(pb.stock[0].length, 2800.0);
    EXPECT_DOUBLE_EQ(pb.stock[0].width, 2070.0);
    EXPECT_EQ(pb.stock[0].quantity, kUnlimited);
    EXPECT_DOUBLE_EQ(pb.stock[0].cost, 60.0);
    ASSERT_EQ(pb.pieces.size(), 3u);
    // Sides: 720 - 0.4 - 0.4 = 719.2 long, 560 - 2 = 558 wide (cut size, not finished size).
    EXPECT_EQ(pb.pieces[0].id, "cab1/side.left");
    EXPECT_EQ(pb.pieces[0].label, "Left side / Right side");
    EXPECT_NEAR(pb.pieces[0].length, 719.2, 1e-9);
    EXPECT_NEAR(pb.pieces[0].width, 558.0, 1e-9);
    EXPECT_EQ(pb.pieces[0].quantity, 2);
    EXPECT_FALSE(pb.pieces[0].canRotate);
    EXPECT_EQ(pb.pieces[0].sourceIds, (std::vector<std::string>{"cab1/side.left", "cab1/side.right"}));
    // Bottom + top: 764 x (560 - 2).
    EXPECT_EQ(pb.pieces[1].label, "Bottom / Top");
    EXPECT_NEAR(pb.pieces[1].length, 764.0, 1e-9);
    EXPECT_NEAR(pb.pieces[1].width, 558.0, 1e-9);
    EXPECT_EQ(pb.pieces[1].quantity, 2);
    EXPECT_FALSE(pb.pieces[1].canRotate);
    // Shelf: 764 x (540 - 2).
    EXPECT_NEAR(pb.pieces[2].length, 764.0, 1e-9);
    EXPECT_NEAR(pb.pieces[2].width, 538.0, 1e-9);
    EXPECT_EQ(pb.pieces[2].quantity, 1);
    EXPECT_FALSE(pb.pieces[2].canRotate);

    const Job& hdf = jobs[1];
    EXPECT_EQ(hdf.id, "mat.hdf3");
    ASSERT_EQ(hdf.pieces.size(), 1u);
    EXPECT_NEAR(hdf.pieces[0].length, 796.0, 1e-9);
    EXPECT_NEAR(hdf.pieces[0].width, 716.0, 1e-9);
    EXPECT_EQ(hdf.pieces[0].quantity, 1);
    EXPECT_TRUE(hdf.pieces[0].canRotate);

    // Both jobs need exactly one sheet (usable 2780 x 2050 with the default 10 mm trim; the
    // five particleboard pieces have 2 * 719.2 * 558 + 2 * 764 * 558 + 764 * 538 = 2066283.2 mm2).
    const CuttingResult rpb = solve(pb);
    expectValid(pb, rpb);
    EXPECT_EQ(rpb.sheets.size(), 1u);
    EXPECT_EQ(rpb.totals.placedCount, 5);
    EXPECT_NEAR(rpb.totals.usedArea, 2066283.2, 1e-6);
    EXPECT_TRUE(rpb.isSheetCountProvenMinimal);
    for (const Placement& p : rpb.sheets[0].placements) EXPECT_FALSE(p.rotated);
    const CuttingResult rhdf = solve(hdf);
    expectValid(hdf, rhdf);
    EXPECT_EQ(rhdf.sheets.size(), 1u);
    EXPECT_NEAR(rhdf.totals.usedArea, 796.0 * 716.0, 1e-6);
}

TEST(FromModel, SettingsAreCopiedIntoEveryJob) {
    const domain::Model model = domain::fixtures::basicCabinet();
    Settings s;
    s.kerf = 3.2;
    s.trim = Trim{5, 6, 7, 8};
    s.minRemnantLength = 400;
    s.minRemnantWidth = 150;
    s.maxStages = 3;
    s.firstCut = FirstCut::Lengthwise;
    s.objective = Objective::MinimizeCuts;
    fos::core::Diagnostics ds;
    for (const Job& job : jobsFromModel(model, s, ds)) {
        EXPECT_DOUBLE_EQ(job.kerf, 3.2);
        EXPECT_DOUBLE_EQ(job.trim.left, 5);
        EXPECT_DOUBLE_EQ(job.trim.right, 6);
        EXPECT_DOUBLE_EQ(job.trim.top, 7);
        EXPECT_DOUBLE_EQ(job.trim.bottom, 8);
        EXPECT_DOUBLE_EQ(job.minRemnantLength, 400);
        EXPECT_DOUBLE_EQ(job.minRemnantWidth, 150);
        EXPECT_EQ(job.maxStages, 3);
        EXPECT_EQ(job.firstCut, FirstCut::Lengthwise);
        EXPECT_EQ(job.objective, Objective::MinimizeCuts);
        EXPECT_TRUE(validateJob(job).empty());
    }
}

TEST(FromModel, GrainAlongWidthSwapsAndGrainlessMaterialAllowsRotation) {
    domain::Model model = domain::fixtures::basicCabinet();
    // The shelf's grain now runs along its width: its cut width 538 must lie along the sheet
    // length, so the piece becomes 538 x 764 (still grain-locked).
    domain::Part* shelf = model.assemblies[0].findPart(domain::PartId("cab1/shelf.1"));
    ASSERT_NE(shelf, nullptr);
    shelf->grain = domain::GrainDirection::AlongWidth;
    fos::core::Diagnostics ds;
    std::vector<Job> jobs = jobsFromModel(model, Settings{}, ds);
    ASSERT_EQ(jobs.size(), 2u);
    ASSERT_EQ(jobs[0].pieces.size(), 3u);
    EXPECT_NEAR(jobs[0].pieces[2].length, 538.0, 1e-9);
    EXPECT_NEAR(jobs[0].pieces[2].width, 764.0, 1e-9);
    EXPECT_FALSE(jobs[0].pieces[2].canRotate);

    // Without grain on the material every part may rotate and nothing is swapped.
    model.materials[0].hasGrain = false;
    jobs = jobsFromModel(model, Settings{}, ds);
    ASSERT_EQ(jobs[0].pieces.size(), 3u);
    for (const Piece& p : jobs[0].pieces) EXPECT_TRUE(p.canRotate);
    EXPECT_NEAR(jobs[0].pieces[2].length, 764.0, 1e-9);
    EXPECT_NEAR(jobs[0].pieces[2].width, 538.0, 1e-9);
    EXPECT_TRUE(ds.empty());
}

TEST(FromModel, ProblemsAreReportedNotDropped) {
    domain::Model model = domain::fixtures::basicCabinet();
    model.assemblies[0].findPart(domain::PartId("cab1/top"))->material = domain::MaterialId("mat.unknown");
    model.assemblies[0].findPart(domain::PartId("cab1/bottom"))->thickness = 16.0;
    model.materials[1].sheetWidth = 0.0; // HDF sheet size invalid
    fos::core::Diagnostics ds;
    const std::vector<Job> jobs = jobsFromModel(model, Settings{}, ds);

    ASSERT_EQ(jobs.size(), 1u); // only particleboard; the HDF job cannot be built
    ASSERT_EQ(jobs[0].pieces.size(), 3u);
    EXPECT_EQ(jobs[0].pieces[1].quantity, 1); // bottom only, the top is missing its material
    EXPECT_EQ(jobs[0].pieces[1].label, "Bottom");

    ASSERT_EQ(ds.size(), 3u);
    bool missing = false, sheet = false, thickness = false;
    for (const auto& d : ds) {
        if (d.code == "cutting.adapter.material_missing") {
            missing = true;
            EXPECT_EQ(d.severity, fos::core::Severity::Error);
            EXPECT_EQ(d.entityId, "cab1/top");
            EXPECT_EQ(d.message, "Part 'Top' (cab1/top) references material 'mat.unknown' which is not in the "
                                 "model; it is not included in any cutting job.");
        }
        if (d.code == "cutting.adapter.invalid_sheet") {
            sheet = true;
            EXPECT_EQ(d.entityId, "mat.hdf3");
        }
        if (d.code == "cutting.adapter.thickness_mismatch") {
            thickness = true;
            EXPECT_EQ(d.severity, fos::core::Severity::Warning);
            EXPECT_EQ(d.entityId, "cab1/bottom");
        }
    }
    EXPECT_TRUE(missing);
    EXPECT_TRUE(sheet);
    EXPECT_TRUE(thickness);
}

TEST(FromModel, EmptyModelGivesNoJobs) {
    fos::core::Diagnostics ds;
    EXPECT_TRUE(jobsFromModel(domain::Model{}, Settings{}, ds).empty());
    EXPECT_TRUE(ds.empty());
}

TEST(FromModel, MissingEdgeBandIsReportedPerEdge) {
    domain::Model model = domain::fixtures::basicCabinet();
    // side.right: 720 x 560, L1 abs2, W1 + W2 abs04. W1 now references a band that does not exist,
    // so it counts as 0 mm: cut length = 720 - 0 - 0.4 = 719.6 (instead of 719.2), width 560 - 2 = 558.
    domain::Part* right = model.assemblies[0].findPart(domain::PartId("cab1/side.right"));
    ASSERT_NE(right, nullptr);
    right->edge(domain::EdgeSide::W1) = domain::EdgeBandId("eb.ghost");
    fos::core::Diagnostics ds;
    const std::vector<Job> jobs = jobsFromModel(model, Settings{}, ds);

    ASSERT_EQ(ds.size(), 1u);
    EXPECT_EQ(ds[0].code, "cutting.adapter.edgeband_missing");
    EXPECT_EQ(ds[0].severity, fos::core::Severity::Warning);
    EXPECT_EQ(ds[0].entityId, "cab1/side.right");
    EXPECT_NE(ds[0].message.find("'Right side' (cab1/side.right)"), std::string::npos) << ds[0].message;
    EXPECT_NE(ds[0].message.find("'eb.ghost'"), std::string::npos);
    EXPECT_NE(ds[0].message.find("edge W1"), std::string::npos);

    // The piece is still produced (oversize), now separate from the left side.
    ASSERT_EQ(jobs.size(), 2u);
    const Piece* oversize = nullptr;
    for (const Piece& p : jobs[0].pieces)
        if (p.id == "cab1/side.right") oversize = &p;
    ASSERT_NE(oversize, nullptr);
    EXPECT_NEAR(oversize->length, 719.6, 1e-9);
    EXPECT_NEAR(oversize->width, 558.0, 1e-9);
    EXPECT_EQ(oversize->quantity, 1);
}

TEST(FromModel, EveryMissingBandEdgeGetsItsOwnWarning) {
    domain::Model model = domain::fixtures::basicCabinet();
    domain::Part* shelf = model.assemblies[0].findPart(domain::PartId("cab1/shelf.1"));
    ASSERT_NE(shelf, nullptr);
    shelf->edge(domain::EdgeSide::L1) = domain::EdgeBandId("eb.x"); // replaces abs2
    shelf->edge(domain::EdgeSide::W2) = domain::EdgeBandId("eb.y");
    fos::core::Diagnostics ds;
    const std::vector<Job> jobs = jobsFromModel(model, Settings{}, ds);
    ASSERT_EQ(ds.size(), 2u); // edge order L1, L2, W1, W2
    EXPECT_NE(ds[0].message.find("'eb.x' on edge L1"), std::string::npos) << ds[0].message;
    EXPECT_NE(ds[1].message.find("'eb.y' on edge W2"), std::string::npos) << ds[1].message;
    // Shelf 764 x 540 with no effective banding: cut 764 x 540 (2 mm wider than with abs2).
    const Piece& p = jobs[0].pieces.back();
    EXPECT_EQ(p.id, "cab1/shelf.1");
    EXPECT_NEAR(p.width, 540.0, 1e-9);
}
