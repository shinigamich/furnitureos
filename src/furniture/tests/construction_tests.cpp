// SPDX-License-Identifier: AGPL-3.0-only
#include "test_support.hpp"

namespace fos::furniture::test {
using domain::EdgeSide;
using domain::Face;
using domain::Groove;
using domain::Hole;
using ::testing::ElementsAre;
using ::testing::HasSubstr;
using ::testing::IsEmpty;

namespace {
GenerationResult withOptions(const ConstructionOptions& o, CabinetDefinition d = fixtureDefinition()) {
    d.construction = o;
    return gen(d);
}
} // namespace

TEST(Construction, TopAndBottomOverSides) {
    ConstructionOptions o;
    o.topJoint = o.bottomJoint = JointKind::OverSides;
    const GenerationResult r = withOptions(o);
    const domain::Assembly& a = r.assembly;
    // Top/bottom run the full 800; sides stand between them: 720 - 2*18 = 684 from z = 18.
    expectSize(part(a, "bottom"), 800, 560, 18);
    expectSize(part(a, "top"), 800, 560, 18);
    expectSize(part(a, "side.left"), 684, 560, 18);
    expectBox(part(a, "bottom"), {0, 0, 0}, {800, 560, 18});
    expectBox(part(a, "top"), {0, 0, 702}, {800, 560, 720});
    expectBox(part(a, "side.left"), {0, 0, 18}, {18, 560, 702});
    expectBox(part(a, "side.right"), {782, 0, 18}, {800, 560, 702});
    expectSize(part(a, "shelf.1"), 764, 540, 18); // interior unchanged
    expectBox(part(a, "shelf.1"), {18, 20, 351}, {782, 560, 369});
    // Banding: the side ends are covered -> raw; the ends of top/bottom are exposed -> hidden band.
    EXPECT_FALSE(part(a, "side.left").edge(EdgeSide::W1).has_value());
    EXPECT_FALSE(part(a, "side.left").edge(EdgeSide::W2).has_value());
    EXPECT_EQ(part(a, "top").edge(EdgeSide::W1)->str(), "eb.abs04");
    EXPECT_EQ(part(a, "bottom").edge(EdgeSide::W2)->str(), "eb.abs04");
    EXPECT_EQ(part(a, "bottom").edge(EdgeSide::L1)->str(), "eb.abs2");
    // Confirmats now go through the bottom from below (face B) into the side's lower end:
    // x = 9 (side mid-plane), y = 50 / 280 / 510.
    EXPECT_THAT(holes(a, "bottom", "hw.joint.side.left.bottom"),
                ElementsAre(Hole{Face::B, 9, 50, 7, 0, true}, Hole{Face::B, 9, 280, 7, 0, true},
                            Hole{Face::B, 9, 510, 7, 0, true}));
    EXPECT_THAT(holes(a, "side.left", "hw.joint.side.left.bottom"),
                ElementsAre(Hole{Face::W1, 50, 9, 5, 37, false}, Hole{Face::W1, 280, 9, 5, 37, false},
                            Hole{Face::W1, 510, 9, 5, 37, false}));
    EXPECT_THAT(errorsOf(r.diagnostics), IsEmpty());
    EXPECT_THAT(interpenetrations(a), IsEmpty());
}

TEST(Construction, MixedJointsTopOverBottomBetween) {
    ConstructionOptions o;
    o.topJoint = JointKind::OverSides;
    const GenerationResult r = withOptions(o);
    expectSize(part(r.assembly, "top"), 800, 560, 18);
    expectSize(part(r.assembly, "bottom"), 764, 560, 18);
    expectSize(part(r.assembly, "side.left"), 702, 560, 18); // 720 - 18, from the floor
    expectBox(part(r.assembly, "side.left"), {0, 0, 0}, {18, 560, 702});
    EXPECT_EQ(part(r.assembly, "side.left").edge(EdgeSide::W1)->str(), "eb.abs04");
    EXPECT_FALSE(part(r.assembly, "side.left").edge(EdgeSide::W2).has_value());
}

TEST(Construction, GroovedBack) {
    ConstructionOptions o;
    o.back = BackMount::Grooved;
    const GenerationResult r = withOptions(o);
    const domain::Assembly& a = r.assembly;
    // groove depth 8, clearance 1 -> engagement 7 per edge.
    // back = (764 + 2*7) x (684 + 2*7) = 778 x 698; rear face 10 mm in front of the rear edge.
    expectSize(part(a, "back"), 778, 698, 3);
    expectBox(part(a, "back"), {11, 547, 11}, {789, 550, 709});
    // Shelves end at the back: interior depth 560 - 10 - 3 = 547 -> shelf 547 - 20 = 527.
    expectSize(part(a, "shelf.1"), 764, 527, 18);
    // Groove centre line y = 560 - 10 - 1.5 = 548.5, 3 wide, 8 deep, on the inner faces, full length.
    const auto groove = [&](const char* slot) {
        std::vector<Groove> out;
        for (const auto& op : part(a, slot).operations)
            if (const auto* g = std::get_if<Groove>(&op.shape)) {
                out.push_back(*g);
                EXPECT_EQ(op.source, "cab1/rule.back_groove");
            }
        return out;
    };
    // Groove end rule: with top and bottom between the sides, both side ends are banded
    // (exposed), so the side grooves stop 1 mm (groove clearance) beyond the back:
    // back z 11..709 -> u 10..710 (side_bottom = 0). Bottom and top end against the sides
    // (hidden ends) -> through grooves 0..764.
    EXPECT_THAT(groove("side.left"), ElementsAre(Groove{Face::B, 10, 548.5, 710, 548.5, 3, 8}));
    EXPECT_THAT(groove("side.right"), ElementsAre(Groove{Face::A, 10, 548.5, 710, 548.5, 3, 8}));
    EXPECT_THAT(groove("bottom"), ElementsAre(Groove{Face::A, 0, 548.5, 764, 548.5, 3, 8}));
    EXPECT_THAT(groove("top"), ElementsAre(Groove{Face::B, 0, 548.5, 764, 548.5, 3, 8}));
    for (const auto& p : a.parts) EXPECT_THAT(errorsOf(machining::validateOperations(p)), IsEmpty()) << p.id.str();
    EXPECT_THAT(errorsOf(r.diagnostics), IsEmpty());
    // The back enters the grooves by exactly 7 mm and touches nothing else.
    EXPECT_THAT(interpenetrations(a), IsEmpty());
    // ... and only because of the grooves: left side x 0..18, groove 8 deep from x = 18 -> x 10..18;
    // the back (x 11..789) shares x 11..18 = 7 mm with it. Without the grooves the 4 carriers
    // (both sides, bottom, top) are reported.
    const auto shared = findInterpenetrations([&] {
        domain::Assembly copy = a;
        for (auto& p : copy.parts) p.operations.clear();
        return copy;
    }());
    ASSERT_EQ(shared.size(), 4u);
    EXPECT_NEAR(shared[0].overlap.size().x, 7.0, 1e-9); // side.left x back: 18 - 11
    EXPECT_NEAR(shared[0].overlap.size().y, 3.0, 1e-9); // the back thickness, y 547..550

    // Changing the groove parameters moves the back consistently: depth 10, clearance 0.5 -> 9.5.
    CabinetDefinition d = fixtureDefinition();
    d.construction = o;
    parametric::Transaction tx;
    tx.setValue("groove_depth", 10).setValue("groove_clearance", 0.5);
    ASSERT_TRUE(d.parameters.apply(tx).ok());
    expectSize(part(gen(d).assembly, "back"), 783, 703, 3); // 764 + 19, 684 + 19
}

TEST(Construction, InsetBackAndNoBack) {
    ConstructionOptions o;
    o.back = BackMount::Inset;
    const GenerationResult r = withOptions(o);
    expectSize(part(r.assembly, "back"), 764, 684, 3);
    expectBox(part(r.assembly, "back"), {18, 557, 18}, {782, 560, 702});
    expectSize(part(r.assembly, "shelf.1"), 764, 537, 18); // 560 - 3 - 20
    EXPECT_THAT(interpenetrations(r.assembly), IsEmpty());

    o.back = BackMount::None;
    CabinetDefinition d = fixtureDefinition();
    d.materials.back = {}; // not needed without a back
    const GenerationResult none = withOptions(o, d);
    EXPECT_FALSE(has(none.assembly, "back"));
    expectSize(part(none.assembly, "shelf.1"), 764, 540, 18);
}

TEST(Construction, RailsAndPlinthPanelWithSidesToTheFloor) {
    ConstructionOptions o;
    o.top = TopKind::Rails;
    o.plinth = PlinthKind::SidesToFloor;
    CabinetDefinition d = fixtureDefinition();
    ASSERT_TRUE(d.parameters.setValue("height", 820).ok());
    const GenerationResult r = withOptions(o, d);
    const domain::Assembly& a = r.assembly;
    EXPECT_FALSE(has(a, "top"));
    expectBox(part(a, "side.left"), {0, 0, 0}, {18, 560, 820});      // sides to the floor
    expectBox(part(a, "bottom"), {18, 0, 100}, {782, 560, 118});     // on top of the 100 mm plinth
    expectBox(part(a, "rail.front"), {18, 0, 802}, {782, 100, 820}); // flat rails 100 wide
    expectBox(part(a, "rail.back"), {18, 460, 802}, {782, 560, 820});
    expectSize(part(a, "rail.front"), 764, 100, 18);
    expectBox(part(a, "plinth"), {18, 50, 0}, {782, 68, 100});       // 50 mm setback
    expectSize(part(a, "plinth"), 764, 100, 18);
    // Overlay back covers the carcass box only: 796 x (820 - 100 - 4) = 716, from z = 102.
    expectBox(part(a, "back"), {2, 560, 102}, {798, 563, 818});
    // Shelf: interior 118..802 = 684 -> z = 118 + 333 = 451.
    expectBox(part(a, "shelf.1"), {18, 20, 451}, {782, 560, 469});
    // Rail joint is 100 long: end offset min(50, 100/4) = 25 -> fasteners at 25 and 75;
    // the rear rail starts at y = 460, i.e. v = 485 and 535 on the side; mid-plane z = 811.
    EXPECT_THAT(holes(a, "side.left", "hw.joint.rail.front.side.left"),
                ElementsAre(Hole{Face::A, 811, 25, 7, 0, true}, Hole{Face::A, 811, 75, 7, 0, true}));
    EXPECT_THAT(holes(a, "side.left", "hw.joint.rail.back.side.left"),
                ElementsAre(Hole{Face::A, 811, 485, 7, 0, true}, Hole{Face::A, 811, 535, 7, 0, true}));
    // Plinth joint (height 100): z = 25 and 75, y = 50 + 9 = 59.
    EXPECT_THAT(holes(a, "side.left", "hw.joint.plinth.side.left"),
                ElementsAre(Hole{Face::A, 25, 59, 7, 0, true}, Hole{Face::A, 75, 59, 7, 0, true}));
    EXPECT_THAT(errorsOf(r.diagnostics), IsEmpty());
    EXPECT_THAT(interpenetrations(a), IsEmpty());
}

TEST(Construction, FeetRaiseTheCarcass) {
    ConstructionOptions o;
    o.plinth = PlinthKind::Feet;
    CabinetDefinition d = fixtureDefinition();
    ASSERT_TRUE(d.parameters.setValue("height", 820).ok());
    const GenerationResult r = withOptions(o, d);
    expectBox(part(r.assembly, "side.left"), {0, 0, 100}, {18, 560, 820});
    expectBox(part(r.assembly, "bottom"), {18, 0, 100}, {782, 560, 118});
    EXPECT_FALSE(has(r.assembly, "plinth"));
    const auto* feet = instance(r.assembly, "hw.feet");
    ASSERT_NE(feet, nullptr);
    EXPECT_EQ(feet->quantity, 4);
    EXPECT_EQ(feet->definition.str(), "hw.foot_adjustable");
    EXPECT_FALSE(hasCode(r.diagnostics, "furniture.plinth.feet_height"));
    ASSERT_TRUE(d.parameters.setValue("plinth_height", 150).ok()); // range of the generic foot: 95..120
    EXPECT_TRUE(hasCode(withOptions(o, d).diagnostics, "furniture.plinth.feet_height"));
}

TEST(Construction, DowelsAndNoFasteners) {
    ConstructionOptions o;
    o.bottomFastener = JointFastener::Dowel;
    o.topFastener = JointFastener::None;
    const GenerationResult r = withOptions(o);
    // Dowel 8 mm: blind 12 mm in the CONTACT face of the side (left: B), 20 mm in the bottom's end.
    EXPECT_THAT(holes(r.assembly, "side.left", "hw.joint.bottom.side.left"),
                ElementsAre(Hole{Face::B, 9, 50, 8, 12, false}, Hole{Face::B, 9, 280, 8, 12, false},
                            Hole{Face::B, 9, 510, 8, 12, false}));
    EXPECT_THAT(holes(r.assembly, "bottom", "hw.joint.bottom.side.left"),
                ::testing::Contains(Hole{Face::W1, 50, 9, 8, 20, false}));
    EXPECT_EQ(instance(r.assembly, "hw.joint.top.side.left"), nullptr);
    EXPECT_THAT(part(r.assembly, "top").operations, IsEmpty());
    EXPECT_THAT(errorsOf(r.diagnostics), IsEmpty());
}

TEST(Construction, UnsupportedCombinationsAreRefusedWithAdvice) {
    CabinetDefinition d = fixtureDefinition();
    const CabinetDefinition original = d;
    ConstructionOptions o;
    o.top = TopKind::Rails;
    o.topJoint = JointKind::OverSides;
    auto r = setConstruction(d, libs(), o);
    ASSERT_FALSE(r.ok());
    EXPECT_THAT(r.error().message, HasSubstr("set the top joint to 'BetweenSides'"));
    o = {};
    o.plinth = PlinthKind::SidesToFloor;
    o.bottomJoint = JointKind::OverSides;
    r = setConstruction(d, libs(), o);
    ASSERT_FALSE(r.ok());
    EXPECT_THAT(r.error().message, HasSubstr("SidesToFloor"));
    EXPECT_EQ(d.construction, original.construction);
}

TEST(Construction, MissingMaterialIsAnError) {
    CabinetDefinition d = fixtureDefinition();
    d.materials.back = domain::MaterialId("mat.unknown");
    auto r = generate(d, libs());
    ASSERT_FALSE(r.ok());
    EXPECT_THAT(r.error().message, HasSubstr("back material 'mat.unknown'"));
}

TEST(Construction, PartOverrides) {
    CabinetDefinition d = fixtureDefinition();
    PartOverride ov;
    ov.material = domain::MaterialId("mat.pb16");
    ov.edges[EdgeSide::L2] = domain::EdgeBandId("eb.abs1");
    ov.edges[EdgeSide::L1] = std::nullopt;
    auto r = setPartOverride(d, libs(), "shelf.1", ov);
    ASSERT_TRUE(r.ok()) << r.error().message;
    const domain::Assembly& assembly = r.value().generated.assembly;
    const domain::Part shelf = part(assembly, "shelf.1");
    EXPECT_EQ(shelf.material.str(), "mat.pb16");
    EXPECT_FALSE(shelf.edge(EdgeSide::L1).has_value());
    EXPECT_EQ(shelf.edge(EdgeSide::L2)->str(), "eb.abs1");
    EXPECT_TRUE(hasCode(r.value().generated.diagnostics, "furniture.material.override_thickness"));
    EXPECT_EQ(part(r.value().generated.assembly, "bottom").material.str(), "mat.pb18");
}

/// Every supported option combination, with a busy interior and fronts.
TEST(Construction, NoInterpenetrationAndValidMachiningForEveryCombination) {
    int combinations = 0;
    for (const auto topJoint : {JointKind::BetweenSides, JointKind::OverSides})
        for (const auto bottomJoint : {JointKind::BetweenSides, JointKind::OverSides})
            for (const auto top : {TopKind::Panel, TopKind::Rails})
                for (const auto back : {BackMount::None, BackMount::Overlay, BackMount::Grooved, BackMount::Inset})
                    for (const auto plinth : {PlinthKind::None, PlinthKind::SidesToFloor, PlinthKind::Feet})
                        for (const auto fastener : {JointFastener::Confirmat, JointFastener::Dowel}) {
                            ConstructionOptions o;
                            o.topJoint = topJoint;
                            o.bottomJoint = bottomJoint;
                            o.top = top;
                            o.back = back;
                            o.plinth = plinth;
                            o.topFastener = o.bottomFastener = o.dividerFastener = fastener;
                            if (!validateConstruction(o).ok()) continue;
                            ++combinations;
                            SCOPED_TRACE(std::string(toString(topJoint)) + "/" + toString(bottomJoint) + "/" +
                                         toString(top) + "/" + toString(back) + "/" + toString(plinth) + "/" +
                                         toString(fastener));
                            CabinetDefinition d = fixtureDefinition();
                            d.construction = o;
                            ASSERT_TRUE(d.parameters.setValue("height", 900).ok());
                            // Left: 2 shelves behind a door; right: fixed shelf, shelf above, drawers below.
                            Section left, right, lower, upper;
                            left.shelfCount = 2;
                            left.front = FrontSpec{};
                            FrontSpec drawers;
                            drawers.kind = FrontKind::DrawerStack;
                            drawers.drawerHeights = {SectionSize::weight(), SectionSize::weight()};
                            lower.size = SectionSize::fixed(300);
                            lower.front = drawers;
                            upper.shelfCount = 1;
                            FrontSpec upperDoor;
                            upperDoor.hingeSide = HingeSide::Right;
                            upper.front = upperDoor;
                            right.split = SplitDirection::Horizontal;
                            right.children = {lower, upper};
                            d.interior = Section{};
                            d.interior.split = SplitDirection::Vertical;
                            d.interior.children = {left, right};
                            const GenerationResult r = gen(d);
                            EXPECT_THAT(errorsOf(r.diagnostics), IsEmpty());
                            EXPECT_THAT(interpenetrations(r.assembly), IsEmpty());
                            std::set<std::string> ids;
                            for (const auto& p : r.assembly.parts) {
                                EXPECT_TRUE(ids.insert(p.id.str()).second) << "duplicate id " << p.id.str();
                                EXPECT_TRUE(p.placement.isRigid());
                                EXPECT_THAT(errorsOf(machining::validateOperations(p)), IsEmpty()) << p.id.str();
                            }
                        }
    // 2*2*2*4*3*2 = 192 combinations.
    // Invalid: Rails top with an OverSides top joint: 1*2*1*4*3*2 = 48.
    // Invalid: SidesToFloor plinth with an OverSides bottom joint: 2*1*2*4*1*2 = 32.
    // Both at once (subtracted twice above): 1*1*1*4*1*2 = 8.
    // Valid: 192 - 48 - 32 + 8 = 120.
    EXPECT_EQ(combinations, 120);
}

} // namespace fos::furniture::test
