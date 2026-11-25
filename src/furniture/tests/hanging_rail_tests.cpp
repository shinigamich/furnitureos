// SPDX-License-Identifier: AGPL-3.0-only
// Hanging (clothes) rails: Section::hangingRail -> rail instance, two supports and their holes.
#include "test_support.hpp"

namespace fos::furniture::test {
using ::testing::HasSubstr;
using ::testing::IsEmpty;

namespace {
/// Wardrobe 1000 x 2000 x 560 (defaults otherwise: 18 mm, overlay back -> interior_depth 560,
/// top / bottom between the sides -> clear height z 18 .. 1982), split vertically into two equal
/// sections: '1' with a hanging rail (default 60 mm below the top), '2' with four shelves.
CabinetDefinition wardrobe(double depth = 560) {
    CabinetDefinition d = defaultCabinet();
    parametric::Transaction tx;
    tx.setValue("width", 1000).setValue("height", 2000).setValue("depth", depth);
    EXPECT_TRUE(d.parameters.apply(tx).ok());
    d.interior.split = SplitDirection::Vertical;
    Section rail = sized(SectionSize::weight());
    rail.hangingRail = HangingRailSpec{};
    d.interior.children = {rail, sized(SectionSize::weight(), 4)};
    return d;
}
void expectHole(const domain::Hole& h, domain::Face face, double u, double v, double depth) {
    EXPECT_EQ(h.face, face);
    EXPECT_NEAR(h.u, u, 1e-9);
    EXPECT_NEAR(h.v, v, 1e-9);
    EXPECT_NEAR(h.diameter, 3, 1e-9);
    EXPECT_NEAR(h.depth, depth, 1e-9);
    EXPECT_FALSE(h.through);
}
} // namespace

TEST(HangingRail, DefaultSectionHasNoRail) {
    EXPECT_FALSE(Section{}.hangingRail.has_value());
    EXPECT_DOUBLE_EQ(HangingRailSpec{}.heightFromTop, 60);
    EXPECT_EQ(HangingRailSpec{}.rail.str(), hardware::ids::kClothesRailGeneric);
    EXPECT_EQ(HangingRailSpec{}.supports.str(), hardware::ids::kRailSupportGeneric);
    const GenerationResult r = gen(defaultCabinet());
    for (const auto& h : r.assembly.hardware) {
        EXPECT_NE(h.definition.str(), hardware::ids::kClothesRailGeneric);
        EXPECT_NE(h.definition.str(), hardware::ids::kRailSupportGeneric);
    }
}

// Inner width 1000 - 2 * 18 = 964; one 18 mm partition leaves 946 -> two sections of 473:
// section '1' x 18 .. 491, partition.1 x 491 .. 509.
// Rail axis: y = (0 + 560) / 2 = 280, z = 1982 - 60 = 1922 (1904 mm above the bottom >= 800).
// Supports: holes at z = 1922 + 16 = 1938 and 1922 - 16 = 1906.
//   side.left (z 0 .. 2000, local x = asm z): face B, u = 1938 / 1906, v = 280, depth 10.
//   partition.1 (origin z = 18, face A towards x = 491): u = 1920 / 1888, v = 280,
//   depth limited by the shared-panel rule to (18 - 2) / 2 = 8.
TEST(HangingRail, RailAndSupportsInWardrobeSection) {
    const GenerationResult r = gen(wardrobe());
    const domain::Assembly& a = r.assembly;
    EXPECT_THAT(errorsOf(r.diagnostics), IsEmpty());
    const auto* rail = instance(a, "hw.rail.1");
    ASSERT_NE(rail, nullptr);
    EXPECT_EQ(rail->definition.str(), hardware::ids::kClothesRailGeneric);
    EXPECT_EQ(rail->quantity, 1);
    EXPECT_DOUBLE_EQ(rail->length, 473);
    EXPECT_TRUE(geometry::approxEqual(rail->position, {18, 280, 1922}));
    ASSERT_EQ(rail->parts.size(), 2u);
    EXPECT_EQ(rail->parts[0].str(), "cab/side.left");
    EXPECT_EQ(rail->parts[1].str(), "cab/partition.1");
    const auto* sup = instance(a, "hw.rail_supports.1");
    ASSERT_NE(sup, nullptr);
    EXPECT_EQ(sup->definition.str(), hardware::ids::kRailSupportGeneric);
    EXPECT_EQ(sup->quantity, 2);

    const auto l = holes(a, "side.left", "hw.rail_supports.1");
    ASSERT_EQ(l.size(), 2u);
    expectHole(l[0], domain::Face::B, 1938, 280, 10);
    expectHole(l[1], domain::Face::B, 1906, 280, 10);
    const auto p = holes(a, "partition.1", "hw.rail_supports.1");
    ASSERT_EQ(p.size(), 2u);
    expectHole(p[0], domain::Face::A, 1920, 280, 8);
    expectHole(p[1], domain::Face::A, 1888, 280, 8);
    // The shelf section gets no rail.
    EXPECT_EQ(instance(a, "hw.rail.2"), nullptr);
}

// Custom height: 100 mm below the top -> axis z = 1982 - 100 = 1882.
TEST(HangingRail, CustomHeightAndRootSection) {
    CabinetDefinition d = wardrobe();
    d.interior.children[0].hangingRail->heightFromTop = 100;
    const GenerationResult r = gen(d);
    const auto* rail = instance(r.assembly, "hw.rail.1");
    ASSERT_NE(rail, nullptr);
    EXPECT_NEAR(rail->position.z, 1882, 1e-9);
    // Root (unsplit) section: rail between both sides, length = inner width 964; ids without path.
    CabinetDefinition root = wardrobe();
    root.interior = Section{};
    root.interior.hangingRail = HangingRailSpec{};
    const GenerationResult rr = gen(root);
    const auto* whole = instance(rr.assembly, "hw.rail");
    ASSERT_NE(whole, nullptr);
    EXPECT_DOUBLE_EQ(whole->length, 964);
    EXPECT_EQ(holes(rr.assembly, "side.right", "hw.rail_supports").size(), 2u);
}

TEST(HangingRail, RefusedWithClearMessages) {
    // Interior depth 450 < section_depth_min 500.
    auto shallow = generate(wardrobe(450), libs());
    ASSERT_FALSE(shallow.ok());
    EXPECT_EQ(shallow.error().code, core::ErrorCode::ConstraintViolation);
    EXPECT_THAT(shallow.error().message, HasSubstr("Section '1' of cabinet 'cab'"));
    EXPECT_THAT(shallow.error().message, HasSubstr("only 450 mm deep"));
    // Default 720 mm cabinet: 1 .. clear height 18 .. 702, axis 642 -> 624 mm < 800 below it.
    CabinetDefinition low = defaultCabinet();
    low.interior.hangingRail = HangingRailSpec{};
    auto l = generate(low, libs());
    ASSERT_FALSE(l.ok());
    EXPECT_THAT(l.error().message, HasSubstr("too low"));
    EXPECT_THAT(l.error().message, HasSubstr("only 624 mm remain"));
    // Too close to the top: 40 < max(25, 15 + 35) = 50.
    CabinetDefinition top = wardrobe();
    top.interior.children[0].hangingRail->heightFromTop = 40;
    auto t = generate(top, libs());
    ASSERT_FALSE(t.ok());
    EXPECT_THAT(t.error().message, HasSubstr("at least 50 mm"));
    // Rail together with shelves.
    CabinetDefinition shelves = wardrobe();
    shelves.interior.children[0].shelfCount = 2;
    auto s = generate(shelves, libs());
    ASSERT_FALSE(s.ok());
    EXPECT_EQ(s.error().code, core::ErrorCode::InvalidArgument);
    EXPECT_THAT(s.error().message, HasSubstr("either a hanging rail or shelves"));
    // Rail behind a drawer stack.
    CabinetDefinition drawers = wardrobe();
    FrontSpec f;
    f.kind = FrontKind::DrawerStack;
    f.drawerHeights = {SectionSize::weight()};
    drawers.interior.children[0].front = f;
    auto dr = generate(drawers, libs());
    ASSERT_FALSE(dr.ok());
    EXPECT_THAT(dr.error().message, HasSubstr("front of kind DrawerStack"));
    // Rail on a split section.
    CabinetDefinition split = wardrobe();
    split.interior.hangingRail = HangingRailSpec{};
    auto sp = generate(split, libs());
    ASSERT_FALSE(sp.ok());
    EXPECT_THAT(sp.error().message, HasSubstr("cannot hold a hanging rail itself"));
    // Unknown support definition.
    CabinetDefinition missing = wardrobe();
    missing.interior.children[0].hangingRail->supports = domain::HardwareId("hw.nope");
    auto m = generate(missing, libs());
    ASSERT_FALSE(m.ok());
    EXPECT_EQ(m.error().code, core::ErrorCode::NotFound);
    EXPECT_THAT(m.error().message, HasSubstr("'hw.nope'"));
}

TEST(HangingRail, SplitMovesRailToFirstChild) {
    CabinetDefinition d = wardrobe();
    d.interior = Section{};
    d.interior.hangingRail = HangingRailSpec{};
    auto r = splitSection(d, libs(), "", SplitDirection::Horizontal, {SectionSize::weight(), SectionSize::weight()});
    ASSERT_TRUE(r.ok()) << r.error().message;
    EXPECT_FALSE(d.interior.hangingRail.has_value());
    EXPECT_TRUE(d.interior.children[0].hangingRail.has_value());
}

} // namespace fos::furniture::test
