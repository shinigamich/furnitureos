// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/production/cut_list.hpp"
#include "test_models.hpp"

#include <gtest/gtest.h>

using namespace fos;
using namespace fos::production;

TEST(CutList, BasicCabinetGroupingAndOrder) {
    const auto m = domain::fixtures::basicCabinet();
    const auto cl = cutList(m);
    EXPECT_TRUE(cl.diagnostics.empty());
    // Material names sort "HDF 3 mm" < "Particleboard 18 mm"; inside a material by
    // descending cut length, then descending cut width, then part number:
    //   0 back            796   x 716  qty 1
    //   1 bottom + top    764   x 558  qty 2   (identical, no machining -> merged)
    //   2 shelf           764   x 538  qty 1
    //   3 left side       719.2 x 558  qty 1   (two holes)
    //   4 right side      719.2 x 558  qty 1   (no holes -> NOT merged with the left side)
    ASSERT_EQ(cl.rows.size(), 5u);
    EXPECT_EQ(cl.rows[0].materialName, "HDF 3 mm");
    EXPECT_EQ(cl.rows[0].quantity, 1);
    EXPECT_NEAR(cl.rows[0].cutLength, 796, 1e-9);

    const auto& bt = cl.rows[1];
    EXPECT_EQ(bt.quantity, 2);
    ASSERT_EQ(bt.partIds.size(), 2u);
    EXPECT_EQ(bt.partIds[0].str(), "cab1/bottom");
    EXPECT_EQ(bt.partIds[1].str(), "cab1/top");
    EXPECT_EQ(bt.partNumbers, (std::vector<int>{3, 4}));
    EXPECT_EQ(bt.name(), "Bottom / Top");
    EXPECT_NEAR(bt.cutLength, 764, 1e-9);
    EXPECT_NEAR(bt.cutWidth, 558, 1e-9);
    EXPECT_NEAR(bt.width, 560, 1e-9);
    EXPECT_EQ(bt.edges[0].name, "ABS 2 mm");
    EXPECT_FALSE(bt.edges[1].banded());

    EXPECT_EQ(cl.rows[2].partIds[0].str(), "cab1/shelf.1");
    EXPECT_NEAR(cl.rows[2].cutWidth, 538, 1e-9);

    EXPECT_EQ(cl.rows[3].partIds[0].str(), "cab1/side.left");
    EXPECT_EQ(cl.rows[3].quantity, 1);
    EXPECT_EQ(cl.rows[3].operationCount, 2);
    EXPECT_FALSE(cl.rows[3].operationSignature.empty());
    EXPECT_EQ(cl.rows[4].partIds[0].str(), "cab1/side.right");
    EXPECT_EQ(cl.rows[4].quantity, 1);
    EXPECT_TRUE(cl.rows[4].operationSignature.empty());
    EXPECT_NEAR(cl.rows[4].cutLength, 719.2, 1e-9);

    int total = 0;
    for (const auto& r : cl.rows) total += r.quantity;
    EXPECT_EQ(total, 6);
}

TEST(CutList, RotationAllowedFlag) {
    auto m = domain::fixtures::basicCabinet();
    auto cl = cutList(m);
    EXPECT_TRUE(cl.rows[0].rotationAllowed);  // HDF back: grain None, material without grain
    EXPECT_FALSE(cl.rows[1].rotationAllowed); // particleboard with grain, part AlongLength
    // Part has a grain direction but the material has no grain -> free rotation.
    m.materials[0].hasGrain = false;
    EXPECT_TRUE(cutList(m).rows[1].rotationAllowed);
    // Grained material but the part itself says None -> free rotation.
    m.materials[0].hasGrain = true;
    for (auto& p : m.assemblies[0].parts) p.grain = domain::GrainDirection::None;
    EXPECT_TRUE(cutList(m).rows[1].rotationAllowed);
}

TEST(CutList, OperationSignatureDefinition) {
    const auto m = domain::fixtures::basicCabinet();
    const auto& left = m.assemblies[0].parts[0];
    // Tokens are sorted as text: "H|B|346|37|..." < "H|B|346|523|..." because '3' < '5'
    // at the first differing character.
    EXPECT_EQ(operationSignature(left), "H|B|346|37|5|13;H|B|346|523|5|13");
    EXPECT_EQ(operationSignature(m.assemblies[0].parts[1]), "");

    // Order of operations, ids, source and note do not matter.
    auto shuffled = left;
    std::swap(shuffled.operations[0], shuffled.operations[1]);
    shuffled.operations[0].id = domain::OperationId("other");
    shuffled.operations[0].source = "user";
    shuffled.operations[1].note = "note";
    EXPECT_EQ(operationSignature(shuffled), operationSignature(left));

    // Any geometric difference does.
    auto moved = left;
    std::get<domain::Hole>(moved.operations[0].shape).u = 347.0;
    EXPECT_NE(operationSignature(moved), operationSignature(left));
    auto otherFace = left;
    std::get<domain::Hole>(otherFace.operations[0].shape).face = domain::Face::A;
    EXPECT_NE(operationSignature(otherFace), operationSignature(left));
    auto through = left;
    std::get<domain::Hole>(through.operations[0].shape).through = true;
    EXPECT_EQ(operationSignature(through), "H|B|346|37|5|T;H|B|346|523|5|13");

    // A groove is the same when its centre line is given in the opposite direction.
    domain::Part a, b;
    domain::Operation ga, gb;
    ga.shape = domain::Groove{domain::Face::B, 0, 10, 600, 10, 4, 8};
    gb.shape = domain::Groove{domain::Face::B, 600, 10, 0, 10, 4, 8};
    a.operations = {ga};
    b.operations = {gb};
    EXPECT_EQ(operationSignature(a), "G|B|0|10|600|10|4|8");
    EXPECT_EQ(operationSignature(a), operationSignature(b));

    domain::Operation pk;
    pk.shape = domain::Pocket{domain::Face::A, {{10, 10}, {60, 10}, {60, 40.5}}, 5, false};
    a.operations = {pk};
    EXPECT_EQ(operationSignature(a), "P|A|5|10,10 60,10 60,40.5");
}

TEST(CutList, SameHolesOnBothSidesMerge) {
    auto m = domain::fixtures::basicCabinet();
    auto& parts = m.assemblies[0].parts;
    parts[1].operations = parts[0].operations; // identical machining, different operation ids
    parts[1].operations[0].id = domain::OperationId("cab1/side.right/op.a");
    parts[1].operations[1].id = domain::OperationId("cab1/side.right/op.b");
    const auto cl = cutList(m);
    ASSERT_EQ(cl.rows.size(), 4u);
    EXPECT_EQ(cl.rows[3].quantity, 2);
    EXPECT_EQ(cl.rows[3].name(), "Left side / Right side");
}

TEST(CutList, DifferencesThatPreventMerging) {
    const auto base = domain::fixtures::basicCabinet();
    auto rowsWith = [&](auto mutate) {
        auto m = base;
        mutate(m.assemblies[0].parts[3]); // the top; normally merged with the bottom
        return cutList(m).rows.size();
    };
    EXPECT_EQ(rowsWith([](domain::Part&) {}), 5u);
    EXPECT_EQ(rowsWith([](domain::Part& p) { p.grain = domain::GrainDirection::AlongWidth; }), 6u);
    EXPECT_EQ(rowsWith([](domain::Part& p) { p.edge(domain::EdgeSide::L2) = domain::EdgeBandId("eb.abs2"); }), 6u);
    // Same band count, other side: L2 instead of L1 gives the same cut size but a different part.
    EXPECT_EQ(rowsWith([](domain::Part& p) {
                  p.edge(domain::EdgeSide::L2) = p.edge(domain::EdgeSide::L1);
                  p.edge(domain::EdgeSide::L1).reset();
              }),
              6u);
    EXPECT_EQ(rowsWith([](domain::Part& p) { p.length = 764.5; }), 6u);
    EXPECT_EQ(rowsWith([](domain::Part& p) { p.material = domain::MaterialId("mat.hdf3"); }), 6u);
    // Name and role never matter.
    EXPECT_EQ(rowsWith([](domain::Part& p) { p.name = "Lid"; p.role = domain::PartRole::Generic; }), 5u);
}

TEST(CutList, SizeTolerance) {
    auto m = domain::fixtures::basicCabinet();
    m.assemblies[0].parts[3].length = 764.04; // top 0.04 mm longer than the bottom
    EXPECT_EQ(cutList(m).rows.size(), 6u);    // default tolerance 1e-6 mm
    CutListOptions o;
    o.sizeTolerance = 0.05;
    const auto cl = cutList(m, o);
    ASSERT_EQ(cl.rows.size(), 5u);
    EXPECT_EQ(cl.rows[1].quantity, 2);
    EXPECT_NEAR(cl.rows[1].length, 764.0, 1e-9); // the group reports its first member
}

TEST(CutList, TwoIdenticalCabinets) {
    const auto m = production::testing::twoCabinets();
    const auto merged = cutList(m);
    // Same five groups as one cabinet, every quantity doubled.
    ASSERT_EQ(merged.rows.size(), 5u);
    EXPECT_EQ(merged.rows[0].quantity, 2); // backs
    EXPECT_EQ(merged.rows[1].quantity, 4); // bottoms + tops
    EXPECT_EQ(merged.rows[1].partNumbers, (std::vector<int>{3, 4, 9, 10}));
    EXPECT_EQ(merged.rows[1].partIds[2].str(), "cab2/bottom");
    ASSERT_EQ(merged.rows[1].assemblyIds.size(), 2u);
    EXPECT_EQ(merged.rows[2].quantity, 2); // shelves
    EXPECT_EQ(merged.rows[3].quantity, 2); // left sides (same holes)
    EXPECT_EQ(merged.rows[3].partNumbers, (std::vector<int>{1, 7}));
    EXPECT_EQ(merged.rows[4].quantity, 2); // right sides

    CutListOptions perAssembly;
    perAssembly.groupAcrossAssemblies = false;
    const auto split = cutList(m, perAssembly);
    ASSERT_EQ(split.rows.size(), 10u);
    EXPECT_EQ(split.rows[0].assemblyIds[0].str(), "cab1"); // tie on size -> lowest part number first
    EXPECT_EQ(split.rows[1].assemblyIds[0].str(), "cab2");
    EXPECT_EQ(split.rows[2].quantity, 2);
    EXPECT_EQ(split.rows[2].partNumbers, (std::vector<int>{3, 4}));
    EXPECT_EQ(split.rows[3].partNumbers, (std::vector<int>{9, 10}));

    const auto scoped = cutList(m, {}, Scope{domain::AssemblyId("cab2")});
    ASSERT_EQ(scoped.rows.size(), 5u);
    EXPECT_EQ(scoped.rows[1].partNumbers, (std::vector<int>{9, 10}));
}

TEST(CutList, DeterministicUnderRegeneration) {
    const auto m = production::testing::twoCabinets();
    const auto a = cutList(m), b = cutList(production::testing::twoCabinets());
    ASSERT_EQ(a.rows.size(), b.rows.size());
    for (std::size_t i = 0; i < a.rows.size(); ++i) {
        EXPECT_EQ(a.rows[i].partIds, b.rows[i].partIds);
        EXPECT_EQ(a.rows[i].operationSignature, b.rows[i].operationSignature);
    }
}
