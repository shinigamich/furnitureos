// SPDX-License-Identifier: AGPL-3.0-only
/// Drawer-box corner joints, per-drawer box overrides and the drawer unit with boxes.
#include "fos/machining/face.hpp"
#include "test_support.hpp"

using namespace fos;
using namespace fos::furniture;
using namespace fos::furniture::test;
using ::testing::HasSubstr;
using ::testing::IsEmpty;

namespace {
/// Default cabinet (600 x 720 x 560, 18 mm carcass) with one full-height drawer front and a
/// ball-bearing box in 16 mm board with a 3 mm HDF bottom (same as drawer_box_tests.cpp).
CabinetDefinition drawerCabinet(JointFastener joint = JointFastener::Dowel) {
    CabinetDefinition d = defaultCabinet();
    d.materials.drawer = domain::MaterialId("mat.pb16");
    FrontSpec f;
    f.kind = FrontKind::DrawerStack;
    f.drawerHeights = {SectionSize::weight(1)};
    f.drawerBox = DrawerBoxSpec{};
    f.drawerBox->joint = joint;
    d.interior.front = f;
    return d;
}

/// Assembly position of a hole's centre on its entry face.
geometry::Vec3 at(const domain::Part& p, const domain::Hole& h) { return machining::faceToAssembly(p, h.face, h.u, h.v); }

void expectNear(geometry::Vec3 got, geometry::Vec3 want) {
    EXPECT_NEAR(got.x, want.x, 1e-6);
    EXPECT_NEAR(got.y, want.y, 1e-6);
    EXPECT_NEAR(got.z, want.z, 1e-6);
}

void expectMachiningValid(const domain::Assembly& a) {
    for (const auto& p : a.parts) EXPECT_THAT(errorsOf(machining::validateOperations(p)), IsEmpty()) << p.id.str();
}
} // namespace

TEST(DrawerJoints, DowelsByHandOnOneBox) {
    const auto r = gen(drawerCabinet());
    const auto& a = r.assembly;
    ASSERT_THAT(errorsOf(r.diagnostics), IsEmpty());
    // Box (drawer_box_tests): x 30.7..569.3, y 0..550, z 33..677; sides 16 thick, 644 high.
    // Groove: lower edge 10 above the underside, 3 mm bottom + groove_fit 0 -> upper edge 13.
    // Zone start = 13 + 5 (drawer_joint_groove_clearance) + 4 (dowel radius) = 22 -> zone
    // 22..644, span 622; end offset min(50, 622 / 4) = 50; n = ceil((622 - 100) / 250) + 1 = 4;
    // positions 50 + k * 522 / 3 = 50, 224, 398, 572 -> heights 72, 246, 420, 594 above the
    // underside -> assembly z = 33 + those = 105, 279, 453, 627.
    const double zs[] = {105, 279, 453, 627};
    struct Joint {
        const char* inst;
        const char* side;
        const char* edge;
        double x, y; // contact plane x (inner face of the side), y = centre of the inner front / back
    };
    const Joint joints[] = {{"hw.joint.drawer.1.box_front.left", "drawer.1.side.left", "drawer.1.box_front", 46.7, 8},
                            {"hw.joint.drawer.1.box_front.right", "drawer.1.side.right", "drawer.1.box_front", 553.3, 8},
                            {"hw.joint.drawer.1.box_back.left", "drawer.1.side.left", "drawer.1.box_back", 46.7, 542},
                            {"hw.joint.drawer.1.box_back.right", "drawer.1.side.right", "drawer.1.box_back", 553.3, 542}};
    for (const Joint& j : joints) {
        const auto* inst = instance(a, j.inst);
        ASSERT_NE(inst, nullptr) << j.inst;
        EXPECT_EQ(inst->definition.str(), hardware::ids::kDowel8x30);
        EXPECT_EQ(inst->quantity, 4);
        const auto face = holes(a, j.side, j.inst);
        const auto edge = holes(a, j.edge, j.inst);
        ASSERT_EQ(face.size(), 4u) << j.inst;
        ASSERT_EQ(edge.size(), 4u) << j.inst;
        for (std::size_t k = 0; k < 4; ++k) {
            // Dowel 8x30: 12 mm face hole into the side's inner face, 20 mm hole into the end edge.
            EXPECT_NEAR(face[k].diameter, 8, 1e-9);
            EXPECT_NEAR(face[k].depth, 12, 1e-9);
            EXPECT_FALSE(face[k].through);
            EXPECT_NEAR(edge[k].depth, 20, 1e-9);
            expectNear(at(part(a, j.side), face[k]), {j.x, j.y, zs[k]});
            expectNear(at(part(a, j.edge), edge[k]), {j.x, j.y, zs[k]});
        }
    }
    // Per side: 2 x 4 dowel holes + 4 slide holes; per inner front / back: 2 x 4 edge holes.
    std::size_t sideHoles = 0, frontHoles = 0;
    for (const auto& op : part(a, "drawer.1.side.left").operations) sideHoles += std::holds_alternative<domain::Hole>(op.shape);
    for (const auto& op : part(a, "drawer.1.box_front").operations) frontHoles += std::holds_alternative<domain::Hole>(op.shape);
    EXPECT_EQ(sideHoles, 12u);
    EXPECT_EQ(frontHoles, 8u);
    // Groove clearance: lowest hole edge z 105 - 4 = 101 >= groove top 33 + 13 = 46 plus 5 mm.
    expectMachiningValid(a);
    EXPECT_THAT(interpenetrations(a), IsEmpty());
}

TEST(DrawerJoints, ConfirmatAndNoneAndUndermount) {
    const auto rc = gen(drawerCabinet(JointFastener::Confirmat));
    ASSERT_THAT(errorsOf(rc.diagnostics), IsEmpty());
    const auto through = holes(rc.assembly, "drawer.1.side.left", "hw.joint.drawer.1.box_front.left");
    const auto pilot = holes(rc.assembly, "drawer.1.box_front", "hw.joint.drawer.1.box_front.left");
    ASSERT_EQ(through.size(), 4u);
    // Confirmat 7x50: radius 3.5 -> zone start 13 + 5 + 3.5 = 21.5, first at 21.5 + 50 = 71.5 -> z 104.5.
    EXPECT_TRUE(through[0].through);
    EXPECT_NEAR(through[0].diameter, 7, 1e-9);
    EXPECT_NEAR(at(part(rc.assembly, "drawer.1.side.left"), through[0]).z, 104.5, 1e-6);
    // Pilot: 50 - 16 + 5 = 39 deep, 5 mm.
    EXPECT_NEAR(pilot[0].depth, 39, 1e-9);
    EXPECT_NEAR(pilot[0].diameter, 5, 1e-9);
    expectMachiningValid(rc.assembly);

    const auto rn = gen(drawerCabinet(JointFastener::None));
    ASSERT_THAT(errorsOf(rn.diagnostics), IsEmpty());
    EXPECT_EQ(instance(rn.assembly, "hw.joint.drawer.1.box_front.left"), nullptr);

    // Undermount: bottom raised 13 -> groove top 16, zone from 25: first dowel at 33 + 75 = 108;
    // the drawer pilot holes in the lower edge stay clear (machining validates, no collision error).
    CabinetDefinition u = drawerCabinet();
    u.interior.front->drawerBox->slide = domain::HardwareId(hardware::ids::kSlideUndermount);
    const auto ru = gen(u);
    ASSERT_THAT(errorsOf(ru.diagnostics), IsEmpty());
    const auto uh = holes(ru.assembly, "drawer.1.side.left", "hw.joint.drawer.1.box_back.left");
    ASSERT_EQ(uh.size(), 4u);
    EXPECT_NEAR(at(part(ru.assembly, "drawer.1.side.left"), uh[0]).z, 108, 1e-6);
    expectMachiningValid(ru.assembly);
}

TEST(DrawerJoints, TooLowForTheGrooveClearance) {
    CabinetDefinition d = drawerCabinet();
    // Clearance 700: zone start 13 + 700 + 4 = 717 > 644 mm sides -> generation fails.
    ASSERT_TRUE(d.parameters
                    .apply(parametric::Transaction().define(
                        parametric::ParameterDef::constant("drawer_joint_groove_clearance", 700)))
                    .ok());
    const auto r = generate(d, libs());
    ASSERT_FALSE(r);
    EXPECT_THAT(r.error().message, HasSubstr("zone above 717 mm"));
}

TEST(DrawerBoxes, PerDrawerOverride) {
    CabinetDefinition d = drawerCabinet();
    FrontSpec& f = *d.interior.front;
    f.drawerHeights = {SectionSize::weight(1), SectionSize::weight(1), SectionSize::weight(1)};
    DrawerBoxSpec under;
    under.slide = domain::HardwareId(hardware::ids::kSlideUndermount);
    under.joint = JointFastener::Confirmat;
    f.drawerBoxes = {DrawerBoxSpec{}, std::nullopt, under};
    // drawerBoxFor: the entries replace the stack-wide box (still set here).
    EXPECT_EQ(drawerBoxFor(f, 0), DrawerBoxSpec{});
    EXPECT_FALSE(drawerBoxFor(f, 1));
    EXPECT_EQ(drawerBoxFor(f, 2), under);
    EXPECT_FALSE(drawerBoxFor(f, 3));
    EXPECT_TRUE(hasDrawerBoxes(f));
    const auto r = gen(d);
    ASSERT_THAT(errorsOf(r.diagnostics), IsEmpty());
    const auto& a = r.assembly;
    // Drawer 1: ball-bearing box 550 long (drawer_box_tests); drawer 2: front only; drawer 3:
    // undermount 500 slide -> box 490 long, confirmat corners.
    EXPECT_NEAR(part(a, "drawer.1.side.left").length, 550, 1e-9);
    EXPECT_TRUE(has(a, "drawer.2"));
    EXPECT_FALSE(has(a, "drawer.2.side.left"));
    EXPECT_EQ(instance(a, "hw.slide.drawer.2.left"), nullptr);
    EXPECT_NEAR(part(a, "drawer.3.side.left").length, 490, 1e-9);
    ASSERT_NE(instance(a, "hw.joint.drawer.3.box_front.left"), nullptr);
    EXPECT_EQ(instance(a, "hw.joint.drawer.3.box_front.left")->definition.str(), hardware::ids::kConfirmat7x50);
    EXPECT_EQ(instance(a, "hw.joint.drawer.1.box_front.left")->definition.str(), hardware::ids::kDowel8x30);
    expectMachiningValid(a);
    EXPECT_THAT(interpenetrations(a), IsEmpty());

    // Empty list: every drawer uses the stack-wide box.
    f.drawerBoxes.clear();
    EXPECT_EQ(drawerBoxFor(f, 1), DrawerBoxSpec{});
    // Size mismatch is refused with both counts.
    f.drawerBoxes = {DrawerBoxSpec{}, std::nullopt};
    const auto bad = generate(d, libs());
    ASSERT_FALSE(bad);
    EXPECT_THAT(bad.error().message, HasSubstr("2 per-drawer box entries but 3 drawer fronts"));
    // All entries empty: no boxes at all.
    f.drawerBoxes = {std::nullopt, std::nullopt, std::nullopt};
    EXPECT_FALSE(hasDrawerBoxes(f));
}

TEST(DrawerBoxes, DrawerUnitWithBoxesTemplate) {
    const CabinetDefinition t = templates::drawerUnitWithBoxes(domain::AssemblyId("du"), templateMaterials());
    EXPECT_EQ(t.name, "Drawer unit with boxes");
    ASSERT_TRUE(t.interior.front && t.interior.front->drawerBox);
    EXPECT_EQ(*t.interior.front->drawerBox, DrawerBoxSpec{});
    const auto r = gen(t);
    ASSERT_THAT(errorsOf(r.diagnostics), IsEmpty());
    for (int i = 1; i <= 4; ++i) {
        const std::string s = "drawer." + std::to_string(i);
        EXPECT_TRUE(has(r.assembly, s + ".side.left")) << s;
        EXPECT_NE(instance(r.assembly, "hw.joint." + s + ".box_back.right"), nullptr) << s;
        EXPECT_NE(instance(r.assembly, "hw.slide." + s + ".left"), nullptr) << s;
    }
    expectMachiningValid(r.assembly);
    EXPECT_THAT(interpenetrations(r.assembly), IsEmpty());
}
