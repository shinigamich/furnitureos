// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/domain/fixtures.hpp"
#include "fos/machining/ordering.hpp"
#include "fos/machining/report.hpp"

#include <gtest/gtest.h>

using namespace fos;
using domain::Face;
using domain::Hole;

namespace {
domain::Operation op(const char* id, domain::OperationShape s, const char* source = "user") {
    return {domain::OperationId(id), std::move(s), source, ""};
}
} // namespace

TEST(Ordering, GroupsByFaceThenToolAndOrdersByNearestNeighbour) {
    domain::Part p;
    p.id = domain::PartId("p");
    p.name = "Panel";
    p.length = 600;
    p.width = 400;
    p.thickness = 18;
    p.operations = {
        op("0", Hole{Face::A, 110, 100, 5, 13, false}),
        op("1", Hole{Face::A, 30, 40, 5, 13, false}),
        op("2", Hole{Face::A, 30, 100, 5, 13, false}),
        op("3", Hole{Face::A, 60, 80, 8, 12, false}),
        op("4", Hole{Face::B, 0, 50, 5, 13, false}),
        op("5", Hole{Face::B, 0, 20, 5, 13, false}),
        op("6", domain::Groove{Face::B, 100, 0, 100, 400, 4, 8}),
        op("7", domain::Groove{Face::B, 130, 400, 130, 0, 4, 8}),
    };
    const auto order = machining::orderOperations(p);
    ASSERT_EQ(order.groups.size(), 4u);

    // Face A, 5 mm drill. From (0,0): |(30,40)| = 50 (3-4-5), |(30,100)| = 104.4, |(110,100)| = 148.7
    //   -> op 1 (50). From (30,40): op 2 is 60 away, op 0 is |(80,60)| = 100 -> op 2 (60).
    //   From (30,100): op 0 is 80 away. Travel 50 + 60 + 80 = 190.
    const auto& g0 = order.groups[0];
    EXPECT_EQ(g0.face, Face::A);
    EXPECT_EQ(g0.tool, machining::ToolKind::Drill);
    EXPECT_DOUBLE_EQ(g0.toolSize, 5);
    EXPECT_EQ(g0.order, (std::vector<std::size_t>{1, 2, 0}));
    EXPECT_NEAR(g0.rapidTravel, 190, 1e-9);

    // Face A, 8 mm drill: single hole at (60,80): |(60,80)| = 100.
    const auto& g1 = order.groups[1];
    EXPECT_EQ(g1.face, Face::A);
    EXPECT_DOUBLE_EQ(g1.toolSize, 8);
    EXPECT_EQ(g1.order, (std::vector<std::size_t>{3}));
    EXPECT_NEAR(g1.rapidTravel, 100, 1e-9);

    // Face B, 5 mm drill: (0,20) first (20), then (0,50) (30). Travel 50.
    const auto& g2 = order.groups[2];
    EXPECT_EQ(g2.face, Face::B);
    EXPECT_EQ(g2.tool, machining::ToolKind::Drill);
    EXPECT_EQ(g2.order, (std::vector<std::size_t>{5, 4}));
    EXPECT_NEAR(g2.rapidTravel, 50, 1e-9);

    // Face B, 4 mm grooves: groove 6 enters at (100,0): 100; it exits at (100,400); groove 7
    // enters at (130,400): 30. Cutting moves are not rapid travel. Travel 130.
    const auto& g3 = order.groups[3];
    EXPECT_EQ(g3.face, Face::B);
    EXPECT_EQ(g3.tool, machining::ToolKind::GrooveCutter);
    EXPECT_DOUBLE_EQ(g3.toolSize, 4);
    EXPECT_EQ(g3.order, (std::vector<std::size_t>{6, 7}));
    EXPECT_NEAR(g3.rapidTravel, 130, 1e-9);

    EXPECT_NEAR(order.totalRapidTravel, 190 + 100 + 50 + 130, 1e-9); // 470
}

TEST(Ordering, TiesKeepLowerIndexAndEmptyPartIsEmpty) {
    domain::Part p;
    p.length = p.width = 500;
    p.thickness = 18;
    EXPECT_TRUE(machining::orderOperations(p).groups.empty());
    EXPECT_DOUBLE_EQ(machining::orderOperations(p).totalRapidTravel, 0);
    // (40,30) and (30,40) are both 50 from the origin -> index 0 first; then |(10,-10)| = 14.142...
    p.operations = {op("0", Hole{Face::A, 40, 30, 5, 13, false}), op("1", Hole{Face::A, 30, 40, 5, 13, false})};
    const auto o = machining::orderOperations(p);
    ASSERT_EQ(o.groups.size(), 1u);
    EXPECT_EQ(o.groups[0].order, (std::vector<std::size_t>{0, 1}));
    EXPECT_NEAR(o.totalRapidTravel, 50 + std::sqrt(200.0), 1e-9);
    // Pockets form their own group, entered at their first contour point.
    p.operations.push_back(op("2", domain::Pocket{Face::A, {{3, 4}, {50, 4}, {50, 50}}, 5, false}));
    const auto o2 = machining::orderOperations(p);
    ASSERT_EQ(o2.groups.size(), 2u);
    EXPECT_EQ(o2.groups[1].tool, machining::ToolKind::PocketMill);
    EXPECT_NEAR(o2.groups[1].rapidTravel, 5, 1e-9);
}

TEST(Report, DrillingRowsOfFixture) {
    auto model = domain::fixtures::basicCabinet();
    auto rows = machining::drillingRows(model);
    // The fixture has exactly two holes: 5 x 13 on face B of the left side at (346,37) and (346,523).
    ASSERT_EQ(rows.size(), 2u);
    EXPECT_EQ(rows[0].assemblyId.str(), "cab1");
    EXPECT_EQ(rows[0].partId.str(), "cab1/side.left");
    EXPECT_EQ(rows[0].partName, "Left side");
    EXPECT_EQ(rows[0].operationId.str(), "cab1/side.left/op.pin.front");
    EXPECT_EQ(rows[0].face, Face::B);
    EXPECT_DOUBLE_EQ(rows[0].u, 346);
    EXPECT_DOUBLE_EQ(rows[0].v, 37);
    EXPECT_DOUBLE_EQ(rows[0].diameter, 5);
    EXPECT_DOUBLE_EQ(rows[0].depth, 13);
    EXPECT_FALSE(rows[0].through);
    EXPECT_EQ(rows[0].source, "cab1/hw.shelf_pins");
    EXPECT_DOUBLE_EQ(rows[1].v, 523);

    const auto summary = machining::drillSummary(model);
    ASSERT_EQ(summary.size(), 1u);
    EXPECT_DOUBLE_EQ(summary[0].diameter, 5);
    EXPECT_EQ(summary[0].holeCount, 2);
    EXPECT_EQ(summary[0].throughCount, 0);
    EXPECT_DOUBLE_EQ(summary[0].maxBlindDepth, 13);
}

TEST(Report, SortingAndSummaryWithMixedHoles) {
    auto model = domain::fixtures::basicCabinet();
    auto& bottom = *model.assemblies[0].findPart(domain::PartId("cab1/bottom"));
    // Deliberately unsorted; a groove must not appear in the drilling report.
    bottom.operations = {
        op("e", Hole{Face::W1, 50, 9, 8, 20, false}),
        op("b2", Hole{Face::B, 200, 30, 7, 0, true}),
        op("a2", Hole{Face::A, 300, 60, 5, 10, false}),
        op("g", domain::Groove{Face::A, 0, 550, 764, 550, 4, 8}),
        op("a1", Hole{Face::A, 300, 50, 5, 10.0000001, false}), // same drill within 1e-6? (diameter is)
        op("a0", Hole{Face::A, 100, 400, 5.0000001, 10, false}),
    };
    const auto rows = machining::drillingRows(bottom);
    ASSERT_EQ(rows.size(), 5u);
    // Face A first (u, then v): a0 (100,400), a1 (300,50), a2 (300,60); then B; then W1.
    EXPECT_EQ(rows[0].operationId.str(), "a0");
    EXPECT_EQ(rows[1].operationId.str(), "a1");
    EXPECT_EQ(rows[2].operationId.str(), "a2");
    EXPECT_EQ(rows[3].operationId.str(), "b2");
    EXPECT_TRUE(rows[3].through);
    EXPECT_DOUBLE_EQ(rows[3].depth, 18); // through hole reports the panel thickness
    EXPECT_EQ(rows[4].operationId.str(), "e");
    EXPECT_EQ(rows[4].face, Face::W1);
    EXPECT_TRUE(rows[0].assemblyId.empty());

    // Whole model: left side 2 x 5 mm (13 deep) + bottom 3 x 5 mm (10 deep), 1 x 7 through, 1 x 8 (20).
    const auto s = machining::drillSummary(model);
    ASSERT_EQ(s.size(), 3u);
    EXPECT_NEAR(s[0].diameter, 5, 1e-6);
    EXPECT_EQ(s[0].holeCount, 5); // 5.0000001 merges with 5
    EXPECT_EQ(s[0].throughCount, 0);
    EXPECT_DOUBLE_EQ(s[0].maxBlindDepth, 13);
    EXPECT_DOUBLE_EQ(s[1].diameter, 7);
    EXPECT_EQ(s[1].holeCount, 1);
    EXPECT_EQ(s[1].throughCount, 1);
    EXPECT_DOUBLE_EQ(s[1].maxBlindDepth, 0);
    EXPECT_DOUBLE_EQ(s[2].diameter, 8);
    EXPECT_EQ(s[2].holeCount, 1);
    EXPECT_DOUBLE_EQ(s[2].maxBlindDepth, 20);
    // Model rows keep model order: left side rows before bottom rows.
    const auto all = machining::drillingRows(model);
    ASSERT_EQ(all.size(), 7u);
    EXPECT_EQ(all[0].partId.str(), "cab1/side.left");
    EXPECT_EQ(all[2].partId.str(), "cab1/bottom");
}
