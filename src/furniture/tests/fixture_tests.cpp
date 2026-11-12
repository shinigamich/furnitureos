// SPDX-License-Identifier: AGPL-3.0-only
#include "test_support.hpp"

namespace fos::furniture::test {
using domain::Face;
using domain::Hole;
using ::testing::ElementsAre;

TEST(Fixture, GeneratedPanelsEqualTheHandWrittenFixture) {
    const domain::Model model = domain::fixtures::basicCabinet();
    const domain::Assembly& expected = model.assemblies.front();
    const GenerationResult r = gen(fixtureDefinition());
    EXPECT_THAT(errorsOf(r.diagnostics), ::testing::IsEmpty());

    ASSERT_EQ(r.assembly.parts.size(), expected.parts.size());
    for (std::size_t i = 0; i < expected.parts.size(); ++i) {
        const domain::Part& e = expected.parts[i];
        const domain::Part& g = r.assembly.parts[i];
        SCOPED_TRACE(e.id.str());
        EXPECT_EQ(g.id, e.id); // same cabinet id "cab1" and same slots, in the same order
        EXPECT_EQ(g.role, e.role);
        EXPECT_DOUBLE_EQ(g.length, e.length);
        EXPECT_DOUBLE_EQ(g.width, e.width);
        EXPECT_DOUBLE_EQ(g.thickness, e.thickness);
        EXPECT_EQ(g.material, e.material);
        EXPECT_EQ(g.edges, e.edges);
        EXPECT_EQ(g.grain, e.grain);
        EXPECT_TRUE(geometry::approxEqual(g.placement, e.placement));
        // Cut sizes follow from finished sizes + banding through domain::cutSize only.
        const auto ce = domain::cutSize(e, model), cg = domain::cutSize(g, toModel(libs(), {r.assembly}));
        EXPECT_DOUBLE_EQ(cg.length, ce.length);
        EXPECT_DOUBLE_EQ(cg.width, ce.width);
    }
}

TEST(Fixture, ShelfPinHolesFollowTheHardwareRule) {
    const GenerationResult r = gen(fixtureDefinition());
    // Shelf underside z = 18 + (684 - 18) / 2 = 351; pin centre = 351 - 5/2 = 348.5.
    // Shelf front edge y = 20: pins at 20 + 17 = 37 and 560 - 37 = 523.
    // Sides: u = local x = height above the side's lower end (z), v = local y = depth.
    const Hole front{Face::B, 348.5, 37.0, 5.0, 13.0, false}, rear{Face::B, 348.5, 523.0, 5.0, 13.0, false};
    EXPECT_THAT(holes(r.assembly, "side.left", "hw.pins.shelf.1"), ElementsAre(front, rear));
    Hole fr = front, rr = rear;
    fr.face = rr.face = Face::A; // inner face of the right side
    EXPECT_THAT(holes(r.assembly, "side.right", "hw.pins.shelf.1"), ElementsAre(fr, rr));
    const auto* pins = instance(r.assembly, "hw.pins.shelf.1");
    ASSERT_NE(pins, nullptr);
    EXPECT_EQ(pins->quantity, 4);
    EXPECT_EQ(pins->definition.str(), "hw.shelf_pin_5");
    EXPECT_TRUE(geometry::approxEqual(pins->position, {18, 37, 348.5}));
}

TEST(Fixture, CornerJointsUseConfirmatsPerRule) {
    const GenerationResult r = gen(fixtureDefinition());
    // Joint along the depth: 560 mm, end offset min(50, 560/4) = 50, n = max(2, ceil(460/250)+1) = 3
    // -> 50, 280, 510. Bottom mid-plane z = 9; top mid-plane z = 702 + 9 = 711.
    // Through holes 7 mm enter from the outer face (left side: A).
    EXPECT_THAT(holes(r.assembly, "side.left", "hw.joint.bottom.side.left"),
                ElementsAre(Hole{Face::A, 9, 50, 7, 0, true}, Hole{Face::A, 9, 280, 7, 0, true},
                            Hole{Face::A, 9, 510, 7, 0, true}));
    EXPECT_THAT(holes(r.assembly, "side.right", "hw.joint.top.side.right"),
                ElementsAre(Hole{Face::B, 711, 50, 7, 0, true}, Hole{Face::B, 711, 280, 7, 0, true},
                            Hole{Face::B, 711, 510, 7, 0, true}));
    // Pilot holes 5 mm in the bottom's left end (W1: u = local y, v = z), depth 50 - 18 + 5 = 37.
    EXPECT_THAT(holes(r.assembly, "bottom", "hw.joint.bottom.side.left"),
                ElementsAre(Hole{Face::W1, 50, 9, 5, 37, false}, Hole{Face::W1, 280, 9, 5, 37, false},
                            Hole{Face::W1, 510, 9, 5, 37, false}));
    int confirmats = 0;
    for (const auto& h : r.assembly.hardware)
        if (h.definition.str() == "hw.confirmat_7x50") confirmats += h.quantity;
    EXPECT_EQ(confirmats, 12); // 4 corners x 3
    for (const auto& p : r.assembly.parts) {
        EXPECT_THAT(errorsOf(machining::validateOperations(p)), ::testing::IsEmpty());
        for (const auto& op : p.operations) EXPECT_NE(op.source, "user");
    }
    EXPECT_THAT(interpenetrations(r.assembly), ::testing::IsEmpty());
}

TEST(Fixture, GenerationIsDeterministic) {
    const CabinetDefinition d = fixtureDefinition();
    EXPECT_EQ(dump(gen(d).assembly), dump(gen(d).assembly));
    EXPECT_FALSE(dump(gen(d).assembly).empty());
}

} // namespace fos::furniture::test
