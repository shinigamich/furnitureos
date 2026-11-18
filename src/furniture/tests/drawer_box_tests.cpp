// SPDX-License-Identifier: AGPL-3.0-only
#include "test_support.hpp"

using namespace fos;
using namespace fos::furniture;
using namespace fos::furniture::test;

namespace {
/// Default cabinet (600 x 720 x 560, 18 mm, overlay back, top / bottom between the sides) with
/// one full-height drawer front carrying a ball-bearing box in 16 mm board, 3 mm HDF bottom.
CabinetDefinition drawerCabinet(DrawerBottomMount bottom = DrawerBottomMount::Grooved) {
    CabinetDefinition d = defaultCabinet();
    d.materials.drawer = domain::MaterialId("mat.pb16");
    FrontSpec f;
    f.kind = FrontKind::DrawerStack;
    f.drawerHeights = {SectionSize::weight(1)};
    f.drawerBox = DrawerBoxSpec{domain::HardwareId(hardware::ids::kSlideBallBearing), bottom};
    d.interior.front = f;
    return d;
}
} // namespace

TEST(DrawerBox, BallBearingBoxSizesAndSlides) {
    const auto r = gen(drawerCabinet());
    const auto& a = r.assembly;
    EXPECT_THAT(errorsOf(r.diagnostics), ::testing::IsEmpty());
    // Opening 600 - 2 * 18 = 564 wide, z 18..702. Box width 564 - 2 * 12.7 = 538.6, x 30.7..569.3.
    // Usable depth 560 (overlay back) - 10 rear clearance = 550 -> slide 550, box length 550.
    // Height: max(front bottom, 18) + 15 = 33 .. min(front top, 702) - 25 = 677 -> 644.
    expectSize(part(a, "drawer.1.side.left"), 550, 644, 16);
    expectBox(part(a, "drawer.1.side.left"), {30.7, 0, 33}, {46.7, 550, 677});
    expectBox(part(a, "drawer.1.side.right"), {553.3, 0, 33}, {569.3, 550, 677});
    // Inner front / back between the sides: 538.6 - 2 * 16 = 506.6.
    expectSize(part(a, "drawer.1.box_front"), 506.6, 644, 16);
    expectBox(part(a, "drawer.1.box_back"), {46.7, 534, 33}, {553.3, 550, 677});
    // Bottom in 6 mm grooves with 1 mm play (engagement 5): 506.6 + 10 = 516.6, 550 - 32 + 10 = 528,
    // 10 mm above the box underside.
    expectSize(part(a, "drawer.1.bottom"), 516.6, 528, 3);
    expectBox(part(a, "drawer.1.bottom"), {41.7, 11, 43}, {558.3, 539, 46});
    // Slides: cabinet holes 37, 165, 293, 421 (<= 550 - 20) at 33 + 22.5 = 55.5 on the inner face of
    // the side; drawer holes 32, 160, 288, 416 (<= 530) on the outer face of the box side.
    for (const char* side : {"left", "right"}) {
        const std::string inst = std::string("hw.slide.drawer.1.") + side;
        ASSERT_NE(instance(a, inst), nullptr) << inst;
        const auto cab = holes(a, std::string("side.") + side, inst);
        const auto drw = holes(a, std::string("drawer.1.side.") + side, inst);
        ASSERT_EQ(cab.size(), 4u);
        ASSERT_EQ(drw.size(), 4u);
        EXPECT_NEAR(drw[3].u, 416, 1e-9);
        EXPECT_NEAR(drw[0].v, 22.5, 1e-9);
        EXPECT_NEAR(cab[0].diameter, 5, 1e-9);
    }
    // Cabinet side: local x = assembly z, local y = assembly y -> (55.5, 37).
    const auto cab = holes(a, "side.left", "hw.slide.drawer.1.left");
    EXPECT_NEAR(cab[0].u, 55.5, 1e-9);
    EXPECT_NEAR(cab[0].v, 37, 1e-9);
    EXPECT_NEAR(cab[3].v, 421, 1e-9);
}

TEST(DrawerBox, RegeneratesWithWidthAndNailedBottom) {
    CabinetDefinition d = drawerCabinet(DrawerBottomMount::NailedUnder);
    ASSERT_TRUE(d.parameters.apply(parametric::Transaction().setValue("width", 800)).ok());
    const auto r = gen(d);
    EXPECT_THAT(errorsOf(r.diagnostics), ::testing::IsEmpty());
    // 800 - 36 - 25.4 = 738.6 outer; bottom full size under the box; sides 644 - 3 = 641 high.
    expectSize(part(r.assembly, "drawer.1.bottom"), 738.6, 550, 3);
    expectSize(part(r.assembly, "drawer.1.box_back"), 706.6, 641, 16);
}

TEST(DrawerBox, Errors) {
    CabinetDefinition d = drawerCabinet();
    // Usable depth 250 - 10 = 240 < 250 (shortest slide).
    ASSERT_TRUE(d.parameters.apply(parametric::Transaction().setValue("depth", 250)).ok());
    const auto shallow = generate(d, libs());
    ASSERT_FALSE(shallow);
    EXPECT_THAT(shallow.error().message, ::testing::HasSubstr("'drawer.1'"));
    EXPECT_THAT(shallow.error().message, ::testing::HasSubstr("240 mm is too shallow"));
    // Undermount runner with 18 mm sides (max 16).
    CabinetDefinition u = drawerCabinet();
    u.materials.drawer = domain::MaterialId("mat.pb18");
    u.interior.front->drawerBox->slide = domain::HardwareId(hardware::ids::kSlideUndermount);
    const auto thick = generate(u, libs());
    ASSERT_FALSE(thick);
    EXPECT_THAT(thick.error().message, ::testing::HasSubstr("too thick for the undermount runner"));
}

TEST(DrawerBox, UndermountAndTemplateAreClean) {
    CabinetDefinition u = drawerCabinet();
    u.interior.front->drawerBox->slide = domain::HardwareId(hardware::ids::kSlideUndermount);
    const auto ru = gen(u);
    EXPECT_THAT(errorsOf(ru.diagnostics), ::testing::IsEmpty());
    // Inner width 564 - 42 = 522 -> outer 554; slide: 550 - 3 allowance -> 500, box 490 long.
    expectSize(part(ru.assembly, "drawer.1.side.left"), 490, 644, 16);
    expectSize(part(ru.assembly, "drawer.1.box_front"), 522, 644, 16);
    // Undermount drawer holes are in the lower edge of the box side: 20, 244, 468 (<= min(500, 490) - 20 = 470).
    EXPECT_EQ(holes(ru.assembly, "drawer.1.side.left", "hw.slide.drawer.1.left").size(), 3u);

    CabinetDefinition t = templates::drawerUnit(domain::AssemblyId("du"), templateMaterials());
    t.interior.front->drawerBox = DrawerBoxSpec{};
    const auto rt = gen(t);
    EXPECT_THAT(errorsOf(rt.diagnostics), ::testing::IsEmpty());
    for (int i = 1; i <= 4; ++i) EXPECT_TRUE(has(rt.assembly, "drawer." + std::to_string(i) + ".bottom")) << i;
}
