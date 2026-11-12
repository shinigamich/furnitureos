// SPDX-License-Identifier: AGPL-3.0-only
// Fronts: doors, door pairs, drawer stacks, hinges and handles. Default cabinet unless
// stated: 600 x 720 x 560, 18 mm, interior x 18..582, z 18..702; full overlay covers an outer
// panel by overlay_outer = 18 - 2 = 16 and a shared one by overlay_shared = 9 - 1.5 = 7.5;
// overlay fronts' inner face at y = -front_offset = -2, outer face at y = -20.
#include "fos/machining/face.hpp"
#include "test_support.hpp"

namespace fos::furniture::test {
using domain::Face;
using domain::Hole;
using ::testing::ElementsAre;
using ::testing::HasSubstr;
using ::testing::IsEmpty;

namespace {
FrontSpec front(FrontKind kind, HandlePosition handle = HandlePosition::Center) {
    FrontSpec f;
    f.kind = kind;
    f.handle = handle;
    return f;
}
geometry::Vec3 holeInAssembly(const domain::Part& p, const Hole& h) {
    return machining::faceToAssembly(p, h.face, h.u, h.v);
}
} // namespace

TEST(Fronts, DoorPairWidthsGapsHingesAndHandles) {
    CabinetDefinition d = defaultCabinet();
    d.interior.front = front(FrontKind::DoorPair);
    const GenerationResult r = gen(d);
    const domain::Assembly& a = r.assembly;
    // Front x 18 - 16 = 2 .. 582 + 16 = 598 (596 wide), z 2..718 (716 high).
    // Each door (596 - 3) / 2 = 296.5: left x 2..298.5, right x 301.5..598 (3 mm gap).
    expectSize(part(a, "door.left"), 716, 296.5, 18);
    expectBox(part(a, "door.left"), {2, -20, 2}, {298.5, -2, 718});
    expectBox(part(a, "door.right"), {301.5, -20, 2}, {598, -2, 718});
    // 716 <= 900 -> 2 hinges at 100 and 716 - 100 = 616 from the door bottom; cup 21.5 from the hinged edge.
    EXPECT_THAT(holes(a, "door.left", "hw.hinges.door.left"),
                ElementsAre(Hole{Face::A, 100, 21.5, 35, 13, false}, Hole{Face::A, 616, 21.5, 35, 13, false}));
    // Face A of a door is its inner face (y = -2).
    EXPECT_NEAR(machining::faceFrameInAssembly(part(a, "door.left"), Face::A).origin.y, -2, 1e-9);
    // Vertical handle 50 from the free edge: left door x = 298.5 - 50 = 248.5, right door 301.5 + 50 = 351.5;
    // centred in height z = 360, holes 128 apart -> z 296 and 424; through holes.
    const auto left = holes(a, "door.left", "hw.handle.door.left");
    ASSERT_EQ(left.size(), 2u);
    EXPECT_TRUE(left[0].through && left[1].through);
    EXPECT_TRUE(geometry::approxEqual(holeInAssembly(part(a, "door.left"), left[0]), {248.5, -2, 296}));
    EXPECT_TRUE(geometry::approxEqual(holeInAssembly(part(a, "door.left"), left[1]), {248.5, -2, 424}));
    const auto right = holes(a, "door.right", "hw.handle.door.right");
    ASSERT_EQ(right.size(), 2u);
    EXPECT_TRUE(geometry::approxEqual(holeInAssembly(part(a, "door.right"), right[0]), {351.5, -2, 296}));
    EXPECT_THAT(errorsOf(r.diagnostics), IsEmpty());
    EXPECT_THAT(interpenetrations(a), IsEmpty());
}

TEST(Fronts, HalfOverlayDoorsShareAPartitionAndAHingeOnItIsLimited) {
    CabinetDefinition d = defaultCabinet();
    Section c1 = sized(SectionSize::weight(1)), c2 = sized(SectionSize::weight(1));
    c1.front = front(FrontKind::Door);
    c1.front->overlay = OverlayKind::Half;
    c2.front = c1.front; // hinged LEFT -> on the partition
    d.interior.split = SplitDirection::Vertical;
    d.interior.children = {c1, c2};
    const GenerationResult r = gen(d);
    const domain::Assembly& a = r.assembly;
    // Columns x 18..291 and 309..582 (partition 291..309). Half overlay covers 7.5 everywhere:
    // door.1 x 10.5..298.5, door.2 x 301.5..589.5 (3 mm gap over the partition), z 10.5..709.5.
    expectBox(part(a, "door.1"), {10.5, -20, 10.5}, {298.5, -2, 709.5});
    expectBox(part(a, "door.2"), {301.5, -20, 10.5}, {589.5, -2, 709.5});
    // door.2 hangs on the partition's face B (x = 309): plate holes limited to (18 - 2) / 2 = 8 mm.
    // Door 699 high -> hinges at 100 and 599 from its bottom (z 110.5 and 609.5); partition local
    // x starts at z 18 -> u = 92.5 -/+ 16 and 591.5 -/+ 16; plate line 37 behind the front edge.
    EXPECT_THAT(holes(a, "partition.1", "hw.hinges.door.2"),
                ElementsAre(Hole{Face::B, 76.5, 37, 5, 8, false}, Hole{Face::B, 108.5, 37, 5, 8, false},
                            Hole{Face::B, 575.5, 37, 5, 8, false}, Hole{Face::B, 607.5, 37, 5, 8, false}));
    // door.1 hangs on the outer left side: catalogue depth 13, u = z (side starts at z 0).
    EXPECT_THAT(holes(a, "side.left", "hw.hinges.door.1"),
                ElementsAre(Hole{Face::B, 94.5, 37, 5, 13, false}, Hole{Face::B, 126.5, 37, 5, 13, false},
                            Hole{Face::B, 593.5, 37, 5, 13, false}, Hole{Face::B, 625.5, 37, 5, 13, false}));
    EXPECT_THAT(errorsOf(r.diagnostics), IsEmpty());
    EXPECT_THAT(interpenetrations(a), IsEmpty());
}

TEST(Fronts, IntermediateHingeAvoidsAShelfAtDefaultShelfClearance) {
    CabinetDefinition d = defaultCabinet();
    ASSERT_TRUE(d.parameters.setValue("height", 1000).ok());
    d.interior.shelfCount = 1;
    d.interior.front = front(FrontKind::Door);
    const GenerationResult r = gen(d);
    // Shelf (1 mm from each side): inner height 964, z = 18 + (964 - 18) / 2 = 491..509.
    // Door z 2..998 = 996 high -> 3 hinges, rule 100 / 498 / 896 from the door bottom. The shelf
    // occupies 489..507 from the door bottom; hinge_shelf_clearance 40 -> forbidden 449..547;
    // 498 is inside, both ends are 49 away, the lower one is taken -> 449.
    EXPECT_THAT(holes(r.assembly, "door", "hw.hinges.door"),
                ElementsAre(Hole{Face::A, 100, 21.5, 35, 13, false}, Hole{Face::A, 449, 21.5, 35, 13, false},
                            Hole{Face::A, 896, 21.5, 35, 13, false}));
    EXPECT_FALSE(hasCode(r.diagnostics, "furniture.hinge.near_shelf"));
    EXPECT_THAT(errorsOf(r.diagnostics), IsEmpty());
}

TEST(Fronts, DrawerStackHeights) {
    CabinetDefinition d = defaultCabinet();
    FrontSpec f = front(FrontKind::DrawerStack);
    f.drawerHeights = {SectionSize::fixed(200), SectionSize::weight(1), SectionSize::weight(1)};
    d.interior.front = f;
    const GenerationResult r = gen(d);
    const domain::Assembly& a = r.assembly;
    // Stack z 2..718 = 716; two 3 mm gaps -> 710: 200, then (710 - 200) / 2 = 255 each.
    // drawer.1 z 2..202, drawer.2 z 205..460, drawer.3 z 463..718; x 2..598.
    expectSize(part(a, "drawer.1"), 596, 200, 18);
    expectSize(part(a, "drawer.2"), 596, 255, 18);
    expectBox(part(a, "drawer.1"), {2, -20, 2}, {598, -2, 202});
    expectBox(part(a, "drawer.2"), {2, -20, 205}, {598, -2, 460});
    expectBox(part(a, "drawer.3"), {2, -20, 463}, {598, -2, 718});
    EXPECT_EQ(part(a, "drawer.1").role, domain::PartRole::DrawerFront);
    EXPECT_THAT(errorsOf(r.diagnostics), IsEmpty());
    EXPECT_THAT(interpenetrations(a), IsEmpty());

    // Fixed heights that do not fit: 400 + 400 = 800 > 716 - 3 = 713 -> 87 mm too much.
    f.drawerHeights = {SectionSize::fixed(400), SectionSize::fixed(400)};
    d.interior.front = f;
    auto bad = generate(d, libs());
    ASSERT_FALSE(bad.ok());
    EXPECT_THAT(bad.error().message, HasSubstr("drawer stack"));
    EXPECT_THAT(bad.error().message, HasSubstr("800 mm but only 713 mm are available (87 mm too much)"));
}

TEST(Fronts, DrawerFrontFaceAIsTheInnerFaceAndHandleHolesGoThrough) {
    CabinetDefinition d = defaultCabinet();
    FrontSpec f = front(FrontKind::DrawerStack);
    f.drawerHeights = {SectionSize::fixed(200), SectionSize::weight(1), SectionSize::weight(1)};
    d.interior.front = f;
    const GenerationResult r = gen(d);
    const domain::Part drawer = part(r.assembly, "drawer.1");
    // Front face rule: face A is the inner (cabinet-side) face at y = -2, face B the outer face at
    // y = -20, exactly like a door.
    const auto faceA = machining::faceFrameInAssembly(drawer, Face::A);
    const auto faceB = machining::faceFrameInAssembly(drawer, Face::B);
    EXPECT_NEAR(faceA.origin.y, -2, 1e-9);
    EXPECT_NEAR(faceB.origin.y, -20, 1e-9);
    EXPECT_TRUE(geometry::approxEqual(faceA.toolDir, {0, -1, 0})); // drilled from the inside towards the viewer
    // Centred horizontal handle: centre x (2 + 598) / 2 = 300, z (2 + 202) / 2 = 102; holes 128 apart
    // -> x 236 and 364. Local frame: x from x0 = 2, y downwards from the top edge z 202:
    // u = 234 / 362, v = 202 - 102 = 100. Through holes recorded on face A.
    const auto hs = holes(r.assembly, "drawer.1", "hw.handle.drawer.1");
    EXPECT_THAT(hs, ElementsAre(Hole{Face::A, 234, 100, 5, 0, true}, Hole{Face::A, 362, 100, 5, 0, true}));
    ASSERT_EQ(hs.size(), 2u);
    EXPECT_TRUE(geometry::approxEqual(holeInAssembly(drawer, hs[0]), {236, -2, 102}));
    EXPECT_TRUE(geometry::approxEqual(holeInAssembly(drawer, hs[1]), {364, -2, 102}));
    // A through hole leaves at the same (u, v) on face B, the visible face, at y = -20.
    EXPECT_TRUE(geometry::approxEqual(machining::faceToAssembly(drawer, Face::B, 234, 100), {236, -20, 102}));
    EXPECT_THAT(errorsOf(machining::validateOperations(drawer)), IsEmpty());
}

TEST(Fronts, InsetDoorGetsNoHingesAndAWarning) {
    CabinetDefinition d = defaultCabinet();
    d.interior.front = front(FrontKind::Door);
    d.interior.front->overlay = OverlayKind::Inset;
    const GenerationResult r = gen(d);
    // Inset gap 2: x 20..580, z 20..700, y 0..18 inside the carcass.
    expectBox(part(r.assembly, "door"), {20, 0, 20}, {580, 18, 700});
    EXPECT_TRUE(hasCode(r.diagnostics, "furniture.front.inset_hinges"));
    EXPECT_EQ(instance(r.assembly, "hw.hinges.door"), nullptr);
    EXPECT_THAT(errorsOf(r.diagnostics), IsEmpty());
    EXPECT_THAT(interpenetrations(r.assembly), IsEmpty());
}

} // namespace fos::furniture::test
