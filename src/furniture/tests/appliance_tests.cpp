// SPDX-License-Identifier: AGPL-3.0-only
// Appliance housings: open appliance niches (FrontKind::ApplianceNiche), decor fronts fixed to
// an appliance door (FrontKind::ApplianceFront) and the appliance gap (ApplianceGapSpec).
#include "fos/geometry/contour.hpp"
#include "test_support.hpp"

namespace fos::furniture::test {
using ::testing::HasSubstr;
using ::testing::IsEmpty;

namespace {
CabinetDefinition oven() { return templates::ovenTallHousing(domain::AssemblyId("ov"), templateMaterials()); }
CabinetDefinition dishwasher() { return templates::dishwasherFront(domain::AssemblyId("dw"), templateMaterials()); }
std::string errorOf(const CabinetDefinition& d) {
    auto r = generate(d, libs());
    return r.ok() ? std::string("(generated)") : r.error().message;
}
/// The single cut-out of a shaped part, as a rectangle in the part frame.
geometry::Rect2 onlyCutout(const domain::Part& p) {
    EXPECT_TRUE(p.shape.has_value()) << p.id.str();
    if (!p.shape || p.shape->cutouts.size() != 1) {
        ADD_FAILURE() << p.id.str() << " should have exactly one cut-out";
        return {};
    }
    return geometry::bounds(p.shape->cutouts[0]);
}
void expectRect(const geometry::Rect2& r, double x0, double y0, double x1, double y1) {
    EXPECT_NEAR(r.min.x, x0, 1e-6);
    EXPECT_NEAR(r.min.y, y0, 1e-6);
    EXPECT_NEAR(r.max.x, x1, 1e-6);
    EXPECT_NEAR(r.max.y, y1, 1e-6);
}
} // namespace

// 600 x 2100 x 560, plinth 100, 18 mm, panel top: interior z 118 .. 2082 (1964), two fixed
// shelves -> 1928 = 700 (drawers) + 595 (niche_height) + 633 (door section).
// fixed_shelf.1 z 818..836 (appliance shelf), niche 836..1431, fixed_shelf.2 1431..1449.
// Drawer fronts: 118 - 16 = 102 .. 818 + 7.5 = 825.5, (723.5 - 3) / 2 = 360.25 each.
// Door: 1449 - 7.5 = 1441.5 .. 2082 + 16 = 2098, x 2..598.
TEST(ApplianceNiche, OvenTemplateGeometry) {
    const GenerationResult r = gen(oven());
    const domain::Assembly& a = r.assembly;
    EXPECT_THAT(errorsOf(r.diagnostics), IsEmpty());
    EXPECT_THAT(interpenetrations(a), IsEmpty());
    expectBox(part(a, "fixed_shelf.1"), {18, 0, 818}, {582, 560, 836});
    expectBox(part(a, "fixed_shelf.2"), {18, 0, 1431}, {582, 560, 1449});
    expectBox(part(a, "drawer.1.1"), {2, -20, 102}, {598, -2, 462.25});
    expectBox(part(a, "drawer.1.2"), {2, -20, 465.25}, {598, -2, 825.5});
    expectBox(part(a, "door.3"), {2, -20, 1441.5}, {598, -2, 2098});
    // The niche itself has no front and no shelves.
    for (const char* s : {"door.2", "flap.2", "appliance_front.2", "shelf.2.1", "drawer.2.1"}) EXPECT_FALSE(has(a, s)) << s;
    EXPECT_EQ(r.values.at("niche_height"), 595);
}

// ShelfSlot: x 18 + 80 = 98 .. 582 - 80 = 502, y 560 - 20 - 50 = 490 .. 540, on fixed_shelf.1
// whose part frame starts at (18, 0): local x 80..484, y 490..540.
TEST(ApplianceNiche, ShelfSlotCutsTheStandingShelf) {
    const GenerationResult r = gen(oven());
    const domain::Part shelf = part(r.assembly, "fixed_shelf.1");
    expectRect(onlyCutout(shelf), 80, 490, 484, 540);
    EXPECT_FALSE(part(r.assembly, "fixed_shelf.2").shape.has_value());
    EXPECT_THAT(messagesWithCode(r.diagnostics, "furniture.contour.machining_outside"), IsEmpty());
}

// BackCutout: overlay back, back_x = back_inset = 2, back_z = 100 + 2 = 102. Region x 98..502,
// z 1431 - 20 - 50 = 1361 .. 1411 -> back frame x 96..500, y 1259..1309.
TEST(ApplianceNiche, BackCutoutCutsTheBackPanel) {
    CabinetDefinition d = oven();
    d.interior.children[1].front->niche.vent = NicheVent::BackCutout;
    const GenerationResult r = gen(d);
    EXPECT_THAT(errorsOf(r.diagnostics), IsEmpty());
    expectRect(onlyCutout(part(r.assembly, "back")), 96, 1259, 500, 1309);
    EXPECT_FALSE(part(r.assembly, "fixed_shelf.1").shape.has_value());

    d.construction.back = BackMount::None;
    const GenerationResult open = gen(d);
    EXPECT_TRUE(hasCode(open.diagnostics, "furniture.appliance.no_back"));
}

TEST(ApplianceNiche, NoVentilationLeavesTheShelfRectangular) {
    CabinetDefinition d = oven();
    d.interior.children[1].front->niche.vent = NicheVent::None;
    EXPECT_FALSE(part(gen(d).assembly, "fixed_shelf.1").shape.has_value());
}

// An override contour of the standing shelf (rounded front corners) keeps its outline and
// receives the ventilation slot as an extra cut-out.
TEST(ApplianceNiche, SlotIsAddedToAnOverrideContour) {
    CabinetDefinition d = oven();
    ContourSpec c;
    c.kind = ContourKind::RoundedCorners;
    c.radii = {"10", "10", "", ""};
    d.materials.overrides["fixed_shelf.1"].contour = c;
    const GenerationResult r = gen(d);
    EXPECT_THAT(errorsOf(r.diagnostics), IsEmpty());
    const domain::Part shelf = part(r.assembly, "fixed_shelf.1");
    expectRect(onlyCutout(shelf), 80, 490, 484, 540);
    EXPECT_GT(shelf.shape->outer.size(), 4u); // two corner arcs added
}

TEST(ApplianceNiche, StructuralErrors) {
    CabinetDefinition d = oven();
    d.interior.children[1].shelfCount = 1;
    EXPECT_THAT(errorOf(d), HasSubstr("is an appliance niche but has 1 adjustable shelves"));

    d = oven();
    d.interior.children[1].front->niche.ventEndMargin = 300; // 564 - 2 x 300 < 0
    EXPECT_THAT(errorOf(d), HasSubstr("564 mm wide; with a ventilation end margin of 300 mm"));

    d = oven();
    d.interior.children[1].front->niche.ventSize = 0;
    EXPECT_THAT(errorOf(d), HasSubstr("ventilation size must be greater than 0 mm"));

    d = oven();
    d.interior.children[1].front->niche.ventSize = 560; // 540 - 560 < 0
    EXPECT_THAT(errorOf(d), HasSubstr("does not fit into the interior depth of 560 mm"));
}

// Width 600 + 18 = 618; right end panel x 600..618, y -(2 + 18) = -20 .. 560, z 0..820.
// Decor front: x 0 + 2 = 2 .. 600 - 3 = 597, z 100 + 2 = 102 .. 820 - 2 = 818, y -20..-2.
TEST(ApplianceGap, DishwasherTemplate) {
    const GenerationResult r = gen(dishwasher());
    const domain::Assembly& a = r.assembly;
    EXPECT_THAT(errorsOf(r.diagnostics), IsEmpty());
    EXPECT_THAT(interpenetrations(a), IsEmpty());
    EXPECT_EQ(slots(a), (std::vector<std::string>{"end_panel.right", "appliance_front"}));
    expectBox(part(a, "end_panel.right"), {600, -20, 0}, {618, 560, 820});
    expectSize(part(a, "end_panel.right"), 820, 580, 18);
    const domain::Part front = part(a, "appliance_front");
    expectBox(front, {2, -20, 102}, {597, -2, 818});
    expectSize(front, 716, 595, 18);
    EXPECT_EQ(front.custom.at(kMountingKey), kMountingApplianceDoor);
    EXPECT_EQ(instance(a, "hw.hinges.appliance_front"), nullptr);
    EXPECT_NE(instance(a, "hw.handle.appliance_front"), nullptr);
    EXPECT_FALSE(hasCode(r.diagnostics, "furniture.appliance.gap_width"));
}

// Both end panels at width 618: gap 618 - 36 = 582 != 600 -> warning; front 18 + 3 = 21 ..
// 600 - 3 = 597. Changing the gap parameter to 582 removes the warning (width follows: 618).
TEST(ApplianceGap, BothPanelsAndGapWidthWarning) {
    CabinetDefinition d = dishwasher();
    d.applianceGap->panels = GapPanels::Both;
    GenerationResult r = gen(d);
    EXPECT_THAT(messagesWithCode(r.diagnostics, "furniture.appliance.gap_width"),
                testing::ElementsAre(HasSubstr("gap of 582 mm")));
    expectBox(part(r.assembly, "end_panel.left"), {0, -20, 0}, {18, 560, 820});
    expectBox(part(r.assembly, "appliance_front"), {21, -20, 102}, {597, -2, 818});
    EXPECT_THAT(interpenetrations(r.assembly), IsEmpty());

    ASSERT_TRUE(d.parameters.setExpression("width", "appliance_gap_width + 2 * thickness").ok());
    r = gen(d);
    EXPECT_FALSE(hasCode(r.diagnostics, "furniture.appliance.gap_width"));
    expectBox(part(r.assembly, "end_panel.right"), {618, -20, 0}, {636, 560, 820});
}

TEST(ApplianceGap, NoFrontAndErrors) {
    CabinetDefinition d = dishwasher();
    d.interior.front.reset();
    d.applianceGap->panels = GapPanels::None;
    ASSERT_TRUE(d.parameters.setExpression("width", "appliance_gap_width").ok());
    EXPECT_TRUE(gen(d).assembly.parts.empty());

    d = dishwasher();
    d.interior.front->kind = FrontKind::Door;
    EXPECT_THAT(errorOf(d), HasSubstr("takes only an ApplianceFront"));
    d = dishwasher();
    d.interior.shelfCount = 1;
    EXPECT_THAT(errorOf(d), HasSubstr("holds only the appliance"));
    d = dishwasher();
    d.corner.kind = CornerKind::Blind;
    EXPECT_THAT(errorOf(d), HasSubstr("cannot also be a corner cabinet"));
}

// A decor front in an ordinary carcass is generated like a single full-overlay door (default
// cabinet 600 x 720: x 2..598, z 2..718) but without hinges.
TEST(ApplianceFront, InOrdinaryCabinet) {
    CabinetDefinition d = defaultCabinet();
    FrontSpec f;
    f.kind = FrontKind::ApplianceFront;
    d.interior.front = f;
    const GenerationResult r = gen(d);
    EXPECT_THAT(errorsOf(r.diagnostics), IsEmpty());
    const domain::Part p = part(r.assembly, "appliance_front");
    expectBox(p, {2, -20, 2}, {598, -2, 718});
    EXPECT_EQ(p.custom.at(kMountingKey), kMountingApplianceDoor);
    EXPECT_TRUE(holes(r.assembly, "side.left", "hw.hinges.appliance_front").empty());
}

TEST(ApplianceEnums, RoundTrip) {
    for (const auto k : {FrontKind::ApplianceNiche, FrontKind::ApplianceFront})
        EXPECT_EQ(fromString<FrontKind>(toString(k)), k);
    for (const auto v : {NicheVent::None, NicheVent::ShelfSlot, NicheVent::BackCutout})
        EXPECT_EQ(fromString<NicheVent>(toString(v)), v);
    for (const auto p : {GapPanels::None, GapPanels::Left, GapPanels::Right, GapPanels::Both})
        EXPECT_EQ(fromString<GapPanels>(toString(p)), p);
}

} // namespace fos::furniture::test
