// SPDX-License-Identifier: AGPL-3.0-only
#include "test_support.hpp"

namespace fos::furniture::test {
using domain::Face;
using domain::Hole;
using ::testing::Contains;
using ::testing::ElementsAre;
using ::testing::HasSubstr;
using ::testing::IsEmpty;

namespace {
CabinetDefinition withDoors() {
    CabinetDefinition d = fixtureDefinition();
    FrontSpec f;
    f.kind = FrontKind::DoorPair;
    d.interior.front = f;
    return d;
}
} // namespace

TEST(Parametric, WidthChangeUpdatesEveryDependentPart) {
    CabinetDefinition d = withDoors();
    const Libraries l = libs();
    const GenerationResult before = gen(d);
    expectSize(part(before.assembly, "bottom"), 764, 560, 18);
    expectSize(part(before.assembly, "back"), 796, 716, 3);
    // Doors, full overlay: (800 - 2*2 reveal - 3 gap) / 2 = 396.5 wide, 720 - 2*2 = 716 high.
    expectSize(part(before.assembly, "door.left"), 716, 396.5, 18);

    auto edit = setParameter(d, l, "width", 900);
    ASSERT_TRUE(edit.ok()) << edit.error().message;
    const domain::Assembly& a = edit.value().generated.assembly;
    EXPECT_THAT(edit.value().changedParameters,
                ElementsAre("back_width", "bottom_length", "inner_width", "top_length", "width"));
    EXPECT_EQ(slots(a), slots(before.assembly)); // ids are stable
    // inner_width = 900 - 2*18 = 864; back = 900 - 2*2 = 896; sides do not change.
    expectSize(part(a, "bottom"), 864, 560, 18);
    expectSize(part(a, "top"), 864, 560, 18);
    expectSize(part(a, "shelf.1"), 864, 540, 18);
    expectSize(part(a, "back"), 896, 716, 3);
    expectSize(part(a, "side.left"), 720, 560, 18);
    expectSize(part(a, "side.right"), 720, 560, 18);
    expectBox(part(a, "side.left"), {0, 0, 0}, {18, 560, 720});
    expectBox(part(a, "side.right"), {882, 0, 0}, {900, 560, 720});
    expectBox(part(a, "shelf.1"), {18, 20, 351}, {882, 560, 369});
    // Doors: (900 - 4 - 3) / 2 = 446.5; left x 2..448.5, right x 451.5..898; y -20..-2 (2 mm offset).
    expectSize(part(a, "door.left"), 716, 446.5, 18);
    expectBox(part(a, "door.left"), {2, -20, 2}, {448.5, -2, 718});
    expectBox(part(a, "door.right"), {451.5, -20, 2}, {898, -2, 718});

    // Hinges: door 716 high (<= 900 -> 2 hinges) at 100 and 716 - 100 = 616 from the door bottom.
    // Door face coords: u = local x (up), v = local y (across). Cup centre 21.5 from the hinged
    // edge: left door v = 21.5 (unchanged); right door v = width - 21.5 = 375 before, 425 after.
    const Hole cupLo{Face::A, 100, 21.5, 35, 13, false}, cupHi{Face::A, 616, 21.5, 35, 13, false};
    for (const auto* asmb : {&before.assembly, &a}) {
        const auto h = holes(*asmb, "door.left", "hw.hinges.door.left");
        EXPECT_THAT(h, ElementsAre(cupLo, cupHi));
    }
    EXPECT_THAT(holes(before.assembly, "door.right", "hw.hinges.door.right"),
                ElementsAre(Hole{Face::A, 100, 375, 35, 13, false}, Hole{Face::A, 616, 375, 35, 13, false}));
    EXPECT_THAT(holes(a, "door.right", "hw.hinges.door.right"),
                ElementsAre(Hole{Face::A, 100, 425, 35, 13, false}, Hole{Face::A, 616, 425, 35, 13, false}));
    // Mounting plates on the left side (inner face B): 37 behind the front edge, at the hinge
    // height z = 2 + 100 = 102 -/+ 16 and z = 2 + 616 = 618 -/+ 16. Independent of the width.
    EXPECT_THAT(holes(a, "side.left", "hw.hinges.door.left"),
                ElementsAre(Hole{Face::B, 86, 37, 5, 13, false}, Hole{Face::B, 118, 37, 5, 13, false},
                            Hole{Face::B, 602, 37, 5, 13, false}, Hole{Face::B, 634, 37, 5, 13, false}));
    // Confirmat joints run along the depth, so a width change leaves their pattern alone,
    // while the pilot holes travel with the panel end (same face coordinates on the longer bottom).
    EXPECT_EQ(holes(a, "bottom", "hw.joint.bottom.side.right"), holes(before.assembly, "bottom", "hw.joint.bottom.side.right"));
    EXPECT_THAT(errorsOf(edit.value().generated.diagnostics), IsEmpty());
    EXPECT_THAT(interpenetrations(a), IsEmpty());
}

TEST(Parametric, HeightChange) {
    CabinetDefinition d = withDoors();
    auto edit = setParameter(d, libs(), "height", 1000);
    ASSERT_TRUE(edit.ok()) << edit.error().message;
    const domain::Assembly& a = edit.value().generated.assembly;
    expectSize(part(a, "side.left"), 1000, 560, 18);
    expectSize(part(a, "back"), 796, 996, 3);         // 1000 - 2*2
    expectBox(part(a, "top"), {18, 0, 982}, {782, 560, 1000});
    // Shelf: inner height 1000 - 36 = 964; z = 18 + (964 - 18) / 2 = 491.
    expectBox(part(a, "shelf.1"), {18, 20, 491}, {782, 560, 509});
    // Door 996 high (z 2 .. 998): > 900 -> 3 hinges; rule positions 100, 498, 896 from the door bottom.
    // The middle one would sit at the shelf (z 491..509 = 489..507 from the door bottom) and its
    // plate holes would collide with the shelf-pin holes. With hinge_shelf_clearance = 40 the
    // forbidden zone is 449..547; both ends are 49 mm from 498, the lower one is taken -> 449.
    EXPECT_THAT(holes(a, "door.left", "hw.hinges.door.left"),
                ElementsAre(Hole{Face::A, 100, 21.5, 35, 13, false}, Hole{Face::A, 449, 21.5, 35, 13, false},
                            Hole{Face::A, 896, 21.5, 35, 13, false}));
    EXPECT_THAT(errorsOf(edit.value().generated.diagnostics), IsEmpty());
}

TEST(Parametric, MiddleHingeKeepsRulePositionWhenNoShelfIsInTheWay) {
    CabinetDefinition d = withDoors();
    // Move the shelf out of the way: with no shelves the 996 mm door keeps 100 / 498 / 896.
    ASSERT_TRUE(setParameter(d, libs(), "height", 1000).ok());
    d.interior.shelfCount = 0;
    auto r = generate(d, libs());
    ASSERT_TRUE(r.ok()) << r.error().message;
    EXPECT_THAT(holes(r.value().assembly, "door.left", "hw.hinges.door.left"),
                ElementsAre(Hole{Face::A, 100, 21.5, 35, 13, false}, Hole{Face::A, 498, 21.5, 35, 13, false},
                            Hole{Face::A, 896, 21.5, 35, 13, false}));
}

TEST(Parametric, DepthChangeRedistributesConfirmats) {
    CabinetDefinition d = fixtureDefinition();
    auto edit = setParameter(d, libs(), "depth", 700);
    ASSERT_TRUE(edit.ok()) << edit.error().message;
    const domain::Assembly& a = edit.value().generated.assembly;
    expectSize(part(a, "side.left"), 720, 700, 18);
    expectSize(part(a, "shelf.1"), 764, 680, 18); // 700 - 20 setback
    expectBox(part(a, "back"), {2, 700, 2}, {798, 703, 718});
    // Joint 700: n = max(2, ceil((700 - 100) / 250) + 1) = 4 -> 50, 250, 450, 650.
    EXPECT_THAT(holes(a, "side.left", "hw.joint.bottom.side.left"),
                ElementsAre(Hole{Face::A, 9, 50, 7, 0, true}, Hole{Face::A, 9, 250, 7, 0, true},
                            Hole{Face::A, 9, 450, 7, 0, true}, Hole{Face::A, 9, 650, 7, 0, true}));
    // Rear shelf pin: 700 - 37 = 663.
    EXPECT_THAT(holes(a, "side.left", "hw.pins.shelf.1"),
                ElementsAre(Hole{Face::B, 348.5, 37, 5, 13, false}, Hole{Face::B, 348.5, 663, 5, 13, false}));
}

TEST(Parametric, CarcassMaterial16mmShiftsEverything) {
    CabinetDefinition d = fixtureDefinition();
    MaterialAssignment m = d.materials;
    m.carcass = domain::MaterialId("mat.pb16");
    auto edit = setMaterials(d, libs(), m);
    ASSERT_TRUE(edit.ok()) << edit.error().message;
    const domain::Assembly& a = edit.value().generated.assembly;
    EXPECT_THAT(edit.value().changedParameters, Contains("thickness"));
    EXPECT_DOUBLE_EQ(d.parameters.value("thickness").value(), 16);
    EXPECT_DOUBLE_EQ(d.parameters.value("inner_width").value(), 768); // 800 - 2*16
    expectSize(part(a, "bottom"), 768, 560, 16);
    expectBox(part(a, "side.left"), {0, 0, 0}, {16, 560, 720});
    expectBox(part(a, "side.right"), {784, 0, 0}, {800, 560, 720});
    expectBox(part(a, "bottom"), {16, 0, 0}, {784, 560, 16});
    expectBox(part(a, "top"), {16, 0, 704}, {784, 560, 720});
    // Shelf: inner height 720 - 32 = 688; z = 16 + (688 - 16) / 2 = 352.
    expectBox(part(a, "shelf.1"), {16, 20, 352}, {784, 560, 368});
    EXPECT_EQ(part(a, "shelf.1").material.str(), "mat.pb16");
    // Confirmat: bottom mid-plane z = 8; pilot depth 50 - 16 + 5 = 39; pins at 352 - 2.5.
    EXPECT_THAT(holes(a, "bottom", "hw.joint.bottom.side.left"), Contains(Hole{Face::W1, 50, 8, 5, 39, false}));
    EXPECT_THAT(holes(a, "side.left", "hw.pins.shelf.1"), Contains(Hole{Face::B, 349.5, 37, 5, 13, false}));
    EXPECT_THAT(errorsOf(edit.value().generated.diagnostics), IsEmpty());
    EXPECT_THAT(interpenetrations(a), IsEmpty());
}

TEST(Parametric, PinnedThicknessThatDisagreesWithTheMaterialIsReported) {
    CabinetDefinition d = fixtureDefinition();
    auto edit = setParameter(d, libs(), "thickness", 19);
    ASSERT_TRUE(edit.ok()) << edit.error().message;
    EXPECT_TRUE(d.pinnedParameters.count("thickness"));
    EXPECT_TRUE(hasCode(edit.value().generated.diagnostics, "furniture.material.thickness_mismatch"));
    expectSize(part(edit.value().generated.assembly, "bottom"), 762, 560, 19); // 800 - 38
    // Resetting follows the material again.
    auto reset = resetParameter(d, libs(), "thickness");
    ASSERT_TRUE(reset.ok()) << reset.error().message;
    EXPECT_FALSE(hasCode(reset.value().generated.diagnostics, "furniture.material.thickness_mismatch"));
    EXPECT_DOUBLE_EQ(d.parameters.value("thickness").value(), 18);
}

TEST(Parametric, ThinnerEdgeBandKeepsFinishedSizesButChangesCutSizes) {
    CabinetDefinition d = fixtureDefinition();
    const Libraries l = libs();
    const GenerationResult before = gen(d);
    MaterialAssignment m = d.materials;
    m.visibleEdge = domain::EdgeBandId("eb.abs1");
    auto edit = setMaterials(d, l, m);
    ASSERT_TRUE(edit.ok()) << edit.error().message;
    const domain::Assembly& a = edit.value().generated.assembly;
    EXPECT_THAT(edit.value().changedParameters, IsEmpty());
    const domain::Model mb = toModel(l, {before.assembly}), ma = toModel(l, {a});
    for (std::size_t i = 0; i < a.parts.size(); ++i) {
        EXPECT_DOUBLE_EQ(a.parts[i].length, before.assembly.parts[i].length);
        EXPECT_DOUBLE_EQ(a.parts[i].width, before.assembly.parts[i].width);
        EXPECT_TRUE(geometry::approxEqual(a.parts[i].placement, before.assembly.parts[i].placement));
    }
    // Side: cut length 720 - 2*0.4 = 719.2 (both); cut width 560 - 2 = 558 before, 560 - 1 = 559 after.
    EXPECT_DOUBLE_EQ(domain::cutSize(part(before.assembly, "side.left"), mb).width, 558);
    EXPECT_DOUBLE_EQ(domain::cutSize(part(a, "side.left"), ma).width, 559);
    EXPECT_DOUBLE_EQ(domain::cutSize(part(a, "side.left"), ma).length, 719.2);
    EXPECT_DOUBLE_EQ(domain::cutSize(part(a, "shelf.1"), ma).width, 539); // 540 - 1
    EXPECT_DOUBLE_EQ(domain::cutSize(part(a, "shelf.1"), ma).length, 764);
}

TEST(Parametric, CustomParametersAndUserExpressionsDriveTheCabinet) {
    CabinetDefinition d = fixtureDefinition();
    const Libraries l = libs();
    ASSERT_TRUE(addParameter(d, l, parametric::ParameterDef::constant("module", 300)).ok());
    ASSERT_TRUE(setParameterExpression(d, l, "width", "2 * module + 200").ok()); // 800
    auto edit = setParameter(d, l, "module", 350);                              // 900
    ASSERT_TRUE(edit.ok()) << edit.error().message;
    EXPECT_THAT(edit.value().changedParameters, Contains("width"));
    expectSize(part(edit.value().generated.assembly, "bottom"), 864, 560, 18);
    // A parameter that others use cannot be removed; the message names the user.
    auto removal = removeParameter(d, l, "module");
    ASSERT_FALSE(removal.ok());
    EXPECT_THAT(removal.error().message, HasSubstr("width"));
    EXPECT_TRUE(d.parameters.contains("module"));
}

TEST(Parametric, OverriddenDerivedParameterIsRespectedAndSurvivesOptionChanges) {
    CabinetDefinition d = fixtureDefinition();
    const Libraries l = libs();
    auto edit = setParameterExpression(d, l, "shelf_depth", "depth / 2"); // 280
    ASSERT_TRUE(edit.ok()) << edit.error().message;
    expectSize(part(edit.value().generated.assembly, "shelf.1"), 764, 280, 18);
    ConstructionOptions o = d.construction;
    o.back = BackMount::Inset;
    auto changed = setConstruction(d, l, o);
    ASSERT_TRUE(changed.ok()) << changed.error().message;
    EXPECT_EQ(d.parameters.find("shelf_depth")->expression, "depth / 2");
    expectSize(part(changed.value().generated.assembly, "shelf.1"), 764, 280, 18);
    // After a reset the standard formula applies: interior depth 560 - 3 = 557, minus 20 = 537.
    auto reset = resetParameter(d, l, "shelf_depth");
    ASSERT_TRUE(reset.ok()) << reset.error().message;
    expectSize(part(reset.value().generated.assembly, "shelf.1"), 764, 537, 18);
}

TEST(Parametric, GraphRoundTripsThroughDefinitionsAndLoad) {
    CabinetDefinition d = withDoors();
    ASSERT_TRUE(resolve(d, libs()).ok());
    auto loaded = parametric::ParameterGraph::load(d.parameters.definitions(), d.parameters.constraints());
    ASSERT_TRUE(loaded.ok()) << loaded.error().message;
    CabinetDefinition copy = d;
    copy.parameters = loaded.value();
    EXPECT_EQ(dump(gen(copy).assembly), dump(gen(d).assembly));
}

} // namespace fos::furniture::test
