// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/production/production.hpp"
#include "test_models.hpp"

#include <gtest/gtest.h>

using namespace fos;
using namespace fos::production;
using domain::EdgeSide;

namespace {
const domain::Model kCab = domain::fixtures::basicCabinet();
}

TEST(PartList, BasicCabinetRows) {
    const auto pl = partList(kCab);
    EXPECT_TRUE(pl.diagnostics.empty());
    ASSERT_EQ(pl.rows.size(), 6u);
    for (int i = 0; i < 6; ++i) EXPECT_EQ(pl.rows[static_cast<std::size_t>(i)].number, i + 1);

    const auto& left = pl.rows[0];
    EXPECT_EQ(left.partId.str(), "cab1/side.left");
    EXPECT_EQ(left.assemblyName, "Base cabinet 800");
    EXPECT_EQ(left.name, "Left side");
    EXPECT_EQ(left.role, domain::PartRole::Side);
    EXPECT_EQ(left.materialName, "Particleboard 18 mm");
    EXPECT_FALSE(left.materialMissing);
    EXPECT_DOUBLE_EQ(left.length, 720);
    EXPECT_DOUBLE_EQ(left.width, 560);
    EXPECT_DOUBLE_EQ(left.thickness, 18);
    // cut length = 720 - 0.4 (W1) - 0.4 (W2) = 719.2; cut width = 560 - 2 (L1) = 558
    EXPECT_NEAR(left.cutLength, 719.2, 1e-9);
    EXPECT_NEAR(left.cutWidth, 558.0, 1e-9);
    EXPECT_EQ(left.grain, domain::GrainDirection::AlongLength);
    EXPECT_EQ(left.edge(EdgeSide::L1).name, "ABS 2 mm");
    EXPECT_FALSE(left.edge(EdgeSide::L2).banded());
    EXPECT_EQ(left.edge(EdgeSide::W1).name, "ABS 0.4 mm");
    EXPECT_DOUBLE_EQ(left.edge(EdgeSide::W2).thickness, 0.4);
    EXPECT_EQ(left.operationCount, 2);
    EXPECT_EQ(pl.rows[1].operationCount, 0);

    const auto& shelf = pl.rows[4];
    EXPECT_EQ(shelf.name, "Shelf");
    EXPECT_NEAR(shelf.cutLength, 764.0, 1e-9); // no banding on W1/W2
    EXPECT_NEAR(shelf.cutWidth, 538.0, 1e-9);  // 540 - 2
    const auto& back = pl.rows[5];
    EXPECT_EQ(back.materialName, "HDF 3 mm");
    EXPECT_NEAR(back.cutLength, 796.0, 1e-9);
    EXPECT_NEAR(back.cutWidth, 716.0, 1e-9);
    EXPECT_NEAR(back.finishedAreaM2(), 0.569936, 1e-12); // 0.796 * 0.716
}

TEST(PartList, ScopeKeepsModelWideNumbers) {
    const auto m = production::testing::twoCabinets();
    const auto pl = partList(m, Scope{domain::AssemblyId("cab2")});
    ASSERT_EQ(pl.rows.size(), 6u);
    EXPECT_EQ(pl.rows.front().number, 7);
    EXPECT_EQ(pl.rows.front().partId.str(), "cab2/side.left");
    EXPECT_EQ(pl.rows.back().number, 12);
}

TEST(MaterialReport, BasicCabinetAreasAndLowerBound) {
    const auto r = materialReport(kCab);
    EXPECT_TRUE(r.diagnostics.empty());
    ASSERT_EQ(r.rows.size(), 2u);
    const auto& pb = r.rows[0]; // model order: particleboard first
    EXPECT_EQ(pb.materialId.str(), "mat.pb18");
    EXPECT_EQ(pb.partCount, 5);
    // Finished: sides 2 x (0.720 x 0.560) = 0.8064
    //           bottom + top 2 x (0.764 x 0.560) = 0.85568
    //           shelf 0.764 x 0.540 = 0.41256                      total 2.07464 m2
    // (fixtures.hpp states 2.3324 m2, which does not match its own part table.)
    EXPECT_NEAR(pb.finishedAreaM2, 2.07464, 1e-9);
    // Cut: sides 2 x (0.7192 x 0.558) = 2 x 0.4013136 = 0.8026272
    //      bottom + top 2 x (0.764 x 0.558) = 2 x 0.426312 = 0.852624
    //      shelf 0.764 x 0.538 = 0.411032                          total 2.0662832 m2
    EXPECT_NEAR(pb.cutAreaM2, 2.0662832, 1e-9);
    EXPECT_NEAR(pb.sheetAreaM2, 5.796, 1e-12); // 2.8 x 2.07
    EXPECT_TRUE(pb.sheetEstimateAvailable);
    EXPECT_EQ(pb.sheetsLowerBoundByArea, 1); // ceil(2.0662832 / 5.796) = ceil(0.3565) = 1
    EXPECT_TRUE(pb.costKnown);
    EXPECT_NEAR(pb.costAtAreaLowerBound, 60.0, 1e-12);

    const auto& hdf = r.rows[1];
    EXPECT_EQ(hdf.partCount, 1);
    EXPECT_NEAR(hdf.finishedAreaM2, 0.569936, 1e-12);
    EXPECT_NEAR(hdf.cutAreaM2, 0.569936, 1e-12); // no banding
    EXPECT_EQ(hdf.sheetsLowerBoundByArea, 1);
    EXPECT_NEAR(hdf.costAtAreaLowerBound, 15.0, 1e-12);
}

TEST(MaterialReport, LowerBoundIsCeilOfAreaRatio) {
    auto m = production::testing::smallModel(); // sheet 1000 x 1000 = 1 m2
    for (int i = 0; i < 3; ++i)
        m.assemblies[0].parts.push_back(production::testing::panel("p" + std::to_string(i), "P", 600, 600, 18, "m1"));
    // 3 x 0.36 = 1.08 m2 -> ceil(1.08 / 1) = 2. (Only one 600 x 600 fits per sheet, so the
    // real count is 3: the figure is a lower bound, exactly as named.)
    auto r = materialReport(m);
    ASSERT_EQ(r.rows.size(), 1u);
    EXPECT_NEAR(r.rows[0].cutAreaM2, 1.08, 1e-12);
    EXPECT_EQ(r.rows[0].sheetsLowerBoundByArea, 2);
    EXPECT_NEAR(r.rows[0].costAtAreaLowerBound, 20.0, 1e-12);

    // Exact multiple: 4 x (500 x 500) = 1.00 m2 -> exactly 1 sheet, not 2.
    m.assemblies[0].parts.clear();
    for (int i = 0; i < 4; ++i)
        m.assemblies[0].parts.push_back(production::testing::panel("q" + std::to_string(i), "Q", 500, 500, 18, "m1"));
    r = materialReport(m);
    EXPECT_EQ(r.rows[0].sheetsLowerBoundByArea, 1);

    // Unknown price: estimate still given, cost not.
    m.materials[0].costPerSheet = 0;
    r = materialReport(m);
    EXPECT_EQ(r.rows[0].sheetsLowerBoundByArea, 1);
    EXPECT_FALSE(r.rows[0].costKnown);
    EXPECT_DOUBLE_EQ(r.rows[0].costAtAreaLowerBound, 0.0);
}

TEST(EdgeBanding, BasicCabinetLengthsAllowancesAndCost) {
    EdgeBandingOptions o;
    o.overhangPerEnd = 25.0;
    o.wastePercent = 10.0;
    const auto r = edgeBandingReport(kCab, o);
    EXPECT_TRUE(r.diagnostics.empty());
    ASSERT_EQ(r.rows.size(), 2u);
    const auto& abs2 = r.rows[0];
    EXPECT_EQ(abs2.edgeBandId.str(), "eb.abs2");
    EXPECT_EQ(abs2.edgeCount, 5);                      // L1 of 2 sides, bottom, top, shelf
    EXPECT_NEAR(abs2.appliedLengthM, 3.732, 1e-9);     // 2 x 0.720 + 3 x 0.764
    EXPECT_NEAR(abs2.overhangAllowanceM, 0.250, 1e-9); // 5 edges x 2 x 0.025
    EXPECT_NEAR(abs2.wasteAllowanceM, 0.3982, 1e-9);   // 10 % of (3.732 + 0.250) = 3.982
    EXPECT_NEAR(abs2.purchaseLengthM, 4.3802, 1e-9);   // 3.982 + 0.3982
    EXPECT_NEAR(abs2.cost, 3.50416, 1e-9);             // 4.3802 x 0.80
    const auto& abs04 = r.rows[1];
    EXPECT_EQ(abs04.edgeCount, 4);                     // W1 + W2 of both sides
    EXPECT_NEAR(abs04.appliedLengthM, 2.240, 1e-9);    // 4 x 0.560
    EXPECT_NEAR(abs04.overhangAllowanceM, 0.200, 1e-9);
    EXPECT_NEAR(abs04.wasteAllowanceM, 0.244, 1e-9);   // 10 % of 2.440
    EXPECT_NEAR(abs04.purchaseLengthM, 2.684, 1e-9);
    EXPECT_NEAR(abs04.cost, 0.8052, 1e-9);             // 2.684 x 0.30

    // No allowances: purchase length == applied length. Negative values count as 0.
    const auto bare = edgeBandingReport(kCab, EdgeBandingOptions{-5.0, -1.0});
    EXPECT_NEAR(bare.rows[0].purchaseLengthM, 3.732, 1e-9);
    EXPECT_DOUBLE_EQ(bare.rows[0].overhangAllowanceM, 0.0);
    EXPECT_DOUBLE_EQ(bare.rows[0].wasteAllowanceM, 0.0);
}

TEST(Hardware, AggregatesByDefinitionAndFlagsUnverified) {
    auto m = production::testing::twoCabinets();
    const auto one = hardwareReport(kCab);
    ASSERT_EQ(one.rows.size(), 1u);
    EXPECT_EQ(one.rows[0].quantity, 4);
    EXPECT_EQ(one.rows[0].instanceCount, 1);
    EXPECT_NEAR(one.rows[0].totalCost, 0.20, 1e-12); // 4 x 0.05
    EXPECT_FALSE(one.rows[0].dimensionsVerified);    // generic definition
    EXPECT_FALSE(one.rows[0].unknownDefinition);
    EXPECT_EQ(one.rows[0].kind, domain::HardwareKind::ShelfSupport);

    m.hardwareCatalog[0].dimensionsVerified = true;
    const auto two = hardwareReport(m);
    ASSERT_EQ(two.rows.size(), 1u);
    EXPECT_EQ(two.rows[0].quantity, 8);
    EXPECT_EQ(two.rows[0].instanceCount, 2);
    EXPECT_NEAR(two.rows[0].totalCost, 0.40, 1e-12);
    EXPECT_TRUE(two.rows[0].dimensionsVerified);
    EXPECT_EQ(hardwareReport(m, Scope{domain::AssemblyId("cab2")}).rows[0].quantity, 4);
}

TEST(Machining, CountsHolesByDiameterAndFace) {
    const auto r = machiningSummary(kCab);
    ASSERT_EQ(r.rows.size(), 1u); // only the left side carries operations
    EXPECT_EQ(r.rows[0].partId.str(), "cab1/side.left");
    EXPECT_EQ(r.rows[0].partNumber, 1);
    ASSERT_EQ(r.rows[0].holes.size(), 1u);
    EXPECT_DOUBLE_EQ(r.rows[0].holes[0].diameter, 5.0);
    EXPECT_EQ(r.rows[0].holes[0].face, domain::Face::B);
    EXPECT_EQ(r.rows[0].holes[0].count, 2);
    EXPECT_EQ(r.totalHoles, 2);
    EXPECT_EQ(machiningSummary(kCab, MachiningOptions{true, 1e-6}).rows.size(), 6u);
}

TEST(Machining, MixedOperations) {
    auto m = production::testing::smallModel();
    auto p = production::testing::panel("p", "Panel", 600, 400, 18, "m1");
    auto add = [&p](domain::OperationShape s) {
        domain::Operation op;
        op.id = domain::OperationId("op" + std::to_string(p.operations.size()));
        op.shape = std::move(s);
        p.operations.push_back(op);
    };
    add(domain::Hole{domain::Face::A, 50, 50, 8.0, 12, false});
    add(domain::Hole{domain::Face::A, 50, 90, 5.0, 12, false});
    add(domain::Hole{domain::Face::W1, 50, 9, 8.0, 30, false});
    add(domain::Hole{domain::Face::A, 80, 50, 8.0, 0, true});
    add(domain::Groove{domain::Face::B, 0, 10, 600, 10, 4, 8});   // length 600
    add(domain::Groove{domain::Face::B, 100, 0, 400, 400, 4, 8}); // hypot(300, 400) = 500
    add(domain::Pocket{domain::Face::A, {{10, 10}, {60, 10}, {60, 40}, {10, 40}}, 5, false});
    m.assemblies[0].parts = {p};
    const auto r = machiningSummary(m);
    ASSERT_EQ(r.rows.size(), 1u);
    const auto& row = r.rows[0];
    EXPECT_EQ(row.holeCount, 4);
    EXPECT_EQ(row.grooveCount, 2);
    EXPECT_EQ(row.pocketCount, 1);
    EXPECT_NEAR(row.grooveLength, 1100.0, 1e-9);
    // Sorted by diameter then face: (5, A) x1, (8, A) x2 [one through], (8, W1) x1
    ASSERT_EQ(row.holes.size(), 3u);
    EXPECT_DOUBLE_EQ(row.holes[0].diameter, 5.0);
    EXPECT_EQ(row.holes[0].count, 1);
    EXPECT_DOUBLE_EQ(row.holes[1].diameter, 8.0);
    EXPECT_EQ(row.holes[1].face, domain::Face::A);
    EXPECT_EQ(row.holes[1].count, 2);
    EXPECT_EQ(row.holes[1].throughCount, 1);
    EXPECT_EQ(row.holes[2].face, domain::Face::W1);
    EXPECT_EQ(row.holes[2].count, 1);
    EXPECT_EQ(machiningTable(r).rows[0][4], "1 x 5 @ A, 2 x 8 @ A, 1 x 8 @ W1");
}

TEST(Cost, BasicCabinetAtAreaLowerBound) {
    CostOptions o;
    o.currency = "UAH";
    const auto c = costEstimate(kCab, o); // default allowances: 2 x 25 mm per edge, 0 % waste
    EXPECT_TRUE(c.diagnostics.empty());
    ASSERT_EQ(c.materials.size(), 2u);
    EXPECT_EQ(c.materials[0].source, SheetCountSource::AreaLowerBound);
    EXPECT_EQ(c.materials[0].sheets, 1);
    EXPECT_NEAR(c.materialsTotal, 75.0, 1e-9); // 1 x 60 + 1 x 15
    // abs2: (3.732 + 0.250) x 0.80 = 3.1856 ; abs04: (2.240 + 0.200) x 0.30 = 0.732
    EXPECT_NEAR(c.edgeBandingTotal, 3.9176, 1e-9);
    EXPECT_NEAR(c.hardwareTotal, 0.20, 1e-9);
    EXPECT_NEAR(c.total, 79.1176, 1e-9);
    EXPECT_TRUE(c.materialsAtAreaLowerBound);
    EXPECT_TRUE(c.complete);
    EXPECT_EQ(c.currency, "UAH");

    const auto t = costTable(c);
    // (2 materials + subtotal) + (2 bands + subtotal) + (1 hardware + subtotal) + total = 3 + 3 + 2 + 1 = 9 rows
    ASSERT_EQ(t.rows.size(), 9u);
    EXPECT_EQ(t.rows.back()[5], "79.12");
    EXPECT_EQ(t.rows.back()[6], "UAH");
    EXPECT_NE(t.rows.back()[7].find("lower bound"), std::string::npos);
    EXPECT_NE(t.rows[0][7].find("area lower bound"), std::string::npos);
}

TEST(Cost, SuppliedSheetCountsReplaceTheBound) {
    CostOptions o;
    o.sheetCounts[domain::MaterialId("mat.pb18")] = 2;
    o.sheetCounts[domain::MaterialId("mat.hdf3")] = 1;
    auto c = costEstimate(kCab, o);
    EXPECT_EQ(c.materials[0].source, SheetCountSource::Supplied);
    EXPECT_EQ(c.materials[0].sheets, 2);
    EXPECT_NEAR(c.materialsTotal, 135.0, 1e-9); // 2 x 60 + 1 x 15
    EXPECT_FALSE(c.materialsAtAreaLowerBound);
    EXPECT_TRUE(c.diagnostics.empty());

    // Only one supplied: the other one stays at the bound and the flag stays set.
    o.sheetCounts.erase(domain::MaterialId("mat.hdf3"));
    c = costEstimate(kCab, o);
    EXPECT_EQ(c.materials[1].source, SheetCountSource::AreaLowerBound);
    EXPECT_TRUE(c.materialsAtAreaLowerBound);

    // A supplied count below the area bound is physically impossible -> flagged.
    o.sheetCounts[domain::MaterialId("mat.pb18")] = 0;
    c = costEstimate(kCab, o);
    ASSERT_EQ(c.diagnostics.size(), 1u);
    EXPECT_EQ(c.diagnostics[0].code, "production.cost.sheet_count_below_bound");
    EXPECT_NE(c.diagnostics[0].message.find("Particleboard 18 mm"), std::string::npos);
}

TEST(Summary, BasicCabinet) {
    const auto s = projectSummary(kCab);
    EXPECT_EQ(s.assemblyCount, 1);
    EXPECT_EQ(s.partCount, 6);
    EXPECT_EQ(s.materialCount, 2);
    EXPECT_EQ(s.edgeBandCount, 2);
    EXPECT_EQ(s.hardwareQuantity, 4);
    EXPECT_EQ(s.operationCount, 2);
    EXPECT_NEAR(s.finishedAreaM2, 2.644576, 1e-9);   // 2.07464 + 0.569936
    EXPECT_NEAR(s.cutAreaM2, 2.6362192, 1e-9);       // 2.0662832 + 0.569936
    EXPECT_NEAR(s.edgeBandingAppliedM, 5.972, 1e-9); // 3.732 + 2.240
    ASSERT_EQ(s.assemblies.size(), 1u);
    // Carcass 800 wide, 720 high, 560 deep + 3 mm back = 563.
    EXPECT_NEAR(s.assemblies[0].envelopeWidth, 800.0, 1e-9);
    EXPECT_NEAR(s.assemblies[0].envelopeDepth, 563.0, 1e-9);
    EXPECT_NEAR(s.assemblies[0].envelopeHeight, 720.0, 1e-9);
    EXPECT_EQ(s.assemblies[0].partCount, 6);

    const auto two = projectSummary(production::testing::twoCabinets());
    EXPECT_EQ(two.assemblyCount, 2);
    EXPECT_EQ(two.partCount, 12);
    EXPECT_EQ(two.materialCount, 2);
    EXPECT_NEAR(two.finishedAreaM2, 5.289152, 1e-9); // 2 x 2.644576
    EXPECT_EQ(summaryTable(two).rows.size(), 2u);
    EXPECT_EQ(summaryTable(two).rows[1][5], "800");
}
