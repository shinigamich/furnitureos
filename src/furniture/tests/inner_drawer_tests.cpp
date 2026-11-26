// SPDX-License-Identifier: AGPL-3.0-only
// Inner drawers behind doors: Section::innerDrawers -> drawer boxes, facias, packers, slides.
#include "test_support.hpp"

namespace fos::furniture::test {
using ::testing::HasSubstr;
using ::testing::IsEmpty;

namespace {

std::string errorOf(const CabinetDefinition& d) {
    auto r = generate(d, libs());
    return r.ok() ? std::string("(generated)") : r.error().message;
}

InnerDrawerSpec twoDrawers(InnerDrawerFront front = InnerDrawerFront::Box) {
    InnerDrawerSpec s;
    s.heights = {SectionSize::weight(), SectionSize::weight()};
    s.front = front;
    return s;
}

/// Wardrobe 600 x 2000 x 560 (18 mm, overlay back -> interior_depth 560, clear height z 18 ..
/// 1982, inner width x 18 .. 582) behind ONE full-overlay door hinged left (root front).
/// Root split horizontally: '1' Fixed 282 (z 18 .. 300), fixed_shelf.1 z 300 .. 318,
/// '2' Fixed 500 (z 318 .. 818) with two inner drawers, fixed_shelf.2 z 818 .. 836, '3' rest.
CabinetDefinition wardrobe(InnerDrawerFront front = InnerDrawerFront::Box) {
    CabinetDefinition d = defaultCabinet();
    parametric::Transaction tx;
    tx.setValue("height", 2000);
    EXPECT_TRUE(d.parameters.apply(tx).ok());
    d.interior.front = FrontSpec{};
    d.interior.split = SplitDirection::Horizontal;
    Section drawers = sized(SectionSize::fixed(500));
    drawers.innerDrawers = twoDrawers(front);
    d.interior.children = {sized(SectionSize::fixed(282)), drawers, sized(SectionSize::weight())};
    return d;
}

} // namespace

TEST(InnerDrawers, EnumTextsAndDefaults) {
    EXPECT_STREQ(toString(InnerDrawerFront::Facia), "Facia");
    EXPECT_EQ(fromString<InnerDrawerFront>("Box"), InnerDrawerFront::Box);
    EXPECT_EQ(fromString<InnerFaciaHandle>("CutOut"), InnerFaciaHandle::CutOut);
    EXPECT_FALSE(fromString<InnerFaciaHandle>("cutout").has_value());
    EXPECT_FALSE(Section{}.innerDrawers.has_value());
    EXPECT_EQ(InnerDrawerSpec{}.slide.str(), hardware::ids::kSlideBallBearing);
}

// Door: full overlay, inner face at y = -front_offset = -2 -> drawer front at y = -2 + 20
// (inner_drawer_door_clearance) = 18. Left side carries the hinges -> ceil(16 / 18) = 1 packer
// x 18 .. 36; the right side carries no hinges -> no packer. Carrying faces x 36 .. 582 (546).
// Ball-bearing: box width 546 - 2 * 12.7 = 520.6, x 309 -/+ 260.3 = 48.7 .. 569.3.
// Depth 560 - 18 - 10 = 532 -> slide 500, box y 18 .. 518.
// Compartments 500 / 2 = 250: [318, 568], [568, 818]. Drawer 1: z 318 + 15 = 333 .. 568 - 25 = 543
// (210 high); drawer 2: z 583 .. 793. Packers 60 high from the box underside: z 333 .. 393, 583 .. 643.
TEST(InnerDrawers, BoxesBehindDoorWithPackerOnHingeSide) {
    const GenerationResult r = gen(wardrobe());
    const domain::Assembly& a = r.assembly;
    EXPECT_THAT(errorsOf(r.diagnostics), IsEmpty());
    EXPECT_THAT(interpenetrations(a), IsEmpty());

    expectBox(part(a, "inner_drawer.2.1.side.left"), {48.7, 18, 333}, {66.7, 518, 543});
    expectBox(part(a, "inner_drawer.2.1.side.right"), {551.3, 18, 333}, {569.3, 518, 543});
    expectBox(part(a, "inner_drawer.2.2.side.left"), {48.7, 18, 583}, {66.7, 518, 793});
    for (const char* s : {"box_front", "box_back", "bottom"}) EXPECT_TRUE(has(a, std::string("inner_drawer.2.1.") + s));
    EXPECT_FALSE(has(a, "inner_drawer.2.1.facia"));

    const domain::Part& packer = part(a, "inner_drawer.2.1.packer.left.1");
    expectSize(packer, 500, 60, 18);
    EXPECT_EQ(packer.role, domain::PartRole::Generic);
    EXPECT_EQ(packer.material.str(), "mat.pb18");
    expectBox(packer, {18, 18, 333}, {36, 518, 393});
    expectBox(part(a, "inner_drawer.2.2.packer.left.1"), {18, 18, 583}, {36, 518, 643});
    EXPECT_FALSE(has(a, "inner_drawer.2.1.packer.right.1"));

    // Left slide is carried by the packer, right slide by the carcass side.
    const auto* left = instance(a, "hw.slide.inner_drawer.2.1.left");
    ASSERT_NE(left, nullptr);
    ASSERT_EQ(left->parts.size(), 2u);
    EXPECT_EQ(left->parts[0].str(), "cab/inner_drawer.2.1.packer.left.1");
    const auto* right = instance(a, "hw.slide.inner_drawer.2.1.right");
    ASSERT_NE(right, nullptr);
    EXPECT_EQ(right->parts[0].str(), "cab/side.right");
    EXPECT_FALSE(holes(a, "inner_drawer.2.1.packer.left.1", "hw.slide.inner_drawer.2.1.left").empty());

    // Door 1996 high -> 4 hinges, 8 plate holes on the left side; none hits a packer.
    EXPECT_EQ(holes(a, "side.left", "hw.hinges.door").size(), 8u);
    EXPECT_FALSE(hasCode(r.diagnostics, "furniture.inner_drawers.hinge_conflict"));
}

// Facia of a 16 mm drawer board: front at y 18 -> facia y 18 .. 34, box front y0 = 34.
// Facia x 36 + 3 .. 582 - 3 = 39 .. 579 (540), drawer 1 z 318 + 3 .. 568 - 3 = 321 .. 565 (244).
// Grip 100 x 30, 20 below the top: u (540 - 100) / 2 = 220 .. 320, v 20 .. 50, through.
// Depth 560 - 34 - 10 = 516 -> slide 500, box y 34 .. 534.
TEST(InnerDrawers, FaciaWithGripCutOut) {
    CabinetDefinition d = wardrobe(InnerDrawerFront::Facia);
    d.materials.drawer = domain::MaterialId("mat.pb16");
    const GenerationResult r = gen(d);
    const domain::Assembly& a = r.assembly;
    EXPECT_THAT(errorsOf(r.diagnostics), IsEmpty());
    EXPECT_THAT(interpenetrations(a), IsEmpty());
    const domain::Part& facia = part(a, "inner_drawer.2.1.facia");
    expectSize(facia, 540, 244, 16);
    EXPECT_EQ(facia.role, domain::PartRole::DrawerFront);
    expectBox(facia, {39, 18, 321}, {579, 34, 565});
    expectBox(part(a, "inner_drawer.2.1.side.left"), {50.7, 34, 333}, {66.7, 534, 543});
    ASSERT_EQ(facia.operations.size(), 1u);
    const auto* pocket = std::get_if<domain::Pocket>(&facia.operations[0].shape);
    ASSERT_NE(pocket, nullptr);
    EXPECT_TRUE(pocket->through);
    EXPECT_EQ(pocket->face, domain::Face::B);
    ASSERT_EQ(pocket->contour.size(), 4u);
    EXPECT_NEAR(pocket->contour[0].x, 220, 1e-9);
    EXPECT_NEAR(pocket->contour[0].y, 20, 1e-9);
    EXPECT_NEAR(pocket->contour[2].x, 320, 1e-9);
    EXPECT_NEAR(pocket->contour[2].y, 50, 1e-9);

    d.interior.children[1].innerDrawers->handle = InnerFaciaHandle::None;
    EXPECT_TRUE(part(gen(d).assembly, "inner_drawer.2.1.facia").operations.empty());
}

// Door pair: both sides carry hinges -> packers left and right; carrying faces 36 .. 564.
TEST(InnerDrawers, DoorPairPacksBothSides) {
    CabinetDefinition d = wardrobe();
    d.interior.front->kind = FrontKind::DoorPair;
    const GenerationResult r = gen(d);
    EXPECT_THAT(errorsOf(r.diagnostics), IsEmpty());
    EXPECT_THAT(interpenetrations(r.assembly), IsEmpty());
    expectBox(part(r.assembly, "inner_drawer.2.1.packer.right.1"), {564, 18, 333}, {582, 518, 393});
    // Box width 528 - 25.4 = 502.6, centred on 300: x 48.7 .. 551.3.
    expectBox(part(r.assembly, "inner_drawer.2.1.side.right"), {533.3, 18, 333}, {551.3, 518, 543});
}

// A hinge projection beyond one board thickness needs two packer layers: ceil(20 / 18) = 2.
TEST(InnerDrawers, ProjectionAboveBoardThicknessStacksPackers) {
    CabinetDefinition d = wardrobe();
    parametric::Transaction tx;
    tx.define(parametric::ParameterDef::constant("inner_drawer_hinge_projection", 20));
    ASSERT_TRUE(d.parameters.apply(tx).ok());
    const GenerationResult r = gen(d);
    EXPECT_THAT(errorsOf(r.diagnostics), IsEmpty());
    expectBox(part(r.assembly, "inner_drawer.2.1.packer.left.2"), {36, 18, 333}, {54, 518, 393});
    const auto* left = instance(r.assembly, "hw.slide.inner_drawer.2.1.left");
    ASSERT_NE(left, nullptr);
    EXPECT_EQ(left->parts[0].str(), "cab/inner_drawer.2.1.packer.left.2");
}

// Kitchen cabinet 600 x 720: door 716 high, bottom hinge at z 2 + 100 = 102 (plate holes z 86 /
// 118); drawer 1 compartment [18, 360] -> packer z 33 .. 93: the plate hole at 86 lies on it.
TEST(InnerDrawers, HingePlateOnPackerIsReported) {
    CabinetDefinition d = defaultCabinet();
    d.interior.front = FrontSpec{};
    d.interior.innerDrawers = twoDrawers();
    auto r = generate(d, libs());
    ASSERT_TRUE(r.ok()) << r.error().message;
    const auto msgs = messagesWithCode(r.value().diagnostics, "furniture.inner_drawers.hinge_conflict");
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_THAT(msgs[0], HasSubstr("hw.hinges.door"));
    EXPECT_THAT(msgs[0], HasSubstr("inner_drawer.1.packer.left.1"));
}

TEST(InnerDrawers, RefusedFrontsAndSections) {
    CabinetDefinition d = defaultCabinet();
    d.interior.innerDrawers = twoDrawers();
    EXPECT_THAT(errorOf(d), HasSubstr("no door in front of it"));
    d.interior.front = FrontSpec{};
    d.interior.front->kind = FrontKind::Flap;
    EXPECT_THAT(errorOf(d), HasSubstr("behind a flap"));
    d.interior.front->kind = FrontKind::DrawerStack;
    d.interior.front->drawerHeights = {SectionSize::weight()};
    EXPECT_THAT(errorOf(d), HasSubstr("behind a drawer stack"));
    d.interior.front = FrontSpec{};
    d.interior.front->kind = FrontKind::ApplianceNiche;
    EXPECT_THAT(errorOf(d), HasSubstr("appliance housing"));
    d.interior.front = FrontSpec{};
    d.interior.shelfCount = 1;
    EXPECT_THAT(errorOf(d), HasSubstr("holds nothing else"));
    d.interior.shelfCount = 0;
    d.interior.innerDrawers->heights.clear();
    EXPECT_THAT(errorOf(d), HasSubstr("without any drawer"));
    d.interior.innerDrawers = twoDrawers();
    d.interior.innerDrawers->slide = domain::HardwareId(hardware::ids::kHingeCup35);
    EXPECT_THAT(errorOf(d), HasSubstr("is not a drawer slide"));
    d.interior.innerDrawers->slide = domain::HardwareId("hw.nope");
    EXPECT_THAT(errorOf(d), HasSubstr("not in the hardware library"));
}

// Sliding doors: drawers start behind the tracks (usable front y 95: setback 10 + depth 80 +
// clearance 5), no packers. Depth 350: 350 - 95 - 10 = 245 < the shortest 250 mm slide.
TEST(InnerDrawers, SlidingDoorsNeedDepthBehindTracks) {
    CabinetDefinition d = wardrobe();
    parametric::Transaction wide;
    wide.setValue("width", 1000);
    ASSERT_TRUE(d.parameters.apply(wide).ok());
    d.interior.front->kind = FrontKind::SlidingDoors;
    const GenerationResult ok = gen(d);
    EXPECT_THAT(errorsOf(ok.diagnostics), IsEmpty());
    EXPECT_THAT(interpenetrations(ok.assembly), IsEmpty());
    EXPECT_FALSE(has(ok.assembly, "inner_drawer.2.1.packer.left.1"));
    EXPECT_NEAR(part(ok.assembly, "inner_drawer.2.1.side.left").bounds().min.y, 95, 1e-9);

    parametric::Transaction tx;
    tx.setValue("depth", 350);
    ASSERT_TRUE(d.parameters.apply(tx).ok());
    EXPECT_THAT(errorOf(d), HasSubstr("behind the sliding doors"));
}

TEST(InnerDrawers, SplitMovesInnerDrawersToFirstChild) {
    CabinetDefinition d = defaultCabinet();
    d.interior.front = FrontSpec{};
    d.interior.innerDrawers = twoDrawers();
    auto r = splitSection(d, libs(), "", SplitDirection::Horizontal, {SectionSize::weight(), SectionSize::weight()});
    ASSERT_TRUE(r.ok()) << r.error().message;
    EXPECT_FALSE(d.interior.innerDrawers.has_value());
    EXPECT_TRUE(d.interior.children[0].innerDrawers.has_value());
}

} // namespace fos::furniture::test
