// SPDX-License-Identifier: AGPL-3.0-only
// Frame-and-panel (five-piece) fronts (README "Frame-and-panel fronts"). Default cabinet:
// 600 x 720 x 560, 18 mm fronts; a full-overlay root front spans x 2..598 (596 wide),
// z 2..718 (716 high), inner face y = -2, outer face y = -20. Generic frame defaults:
// stiles / rails 70 mm, panel 8 mm, groove 10 mm deep, panel clearance 2 mm, groove_fit 0.
#include "fos/machining/face.hpp"
#include "test_support.hpp"

namespace fos::furniture::test {
using domain::Face;
using domain::Groove;
using domain::Hole;
using ::testing::ElementsAre;
using ::testing::HasSubstr;
using ::testing::IsEmpty;

namespace {
FrontSpec framed(FrontKind kind, FrameJoint joint = FrameJoint::StubTenon) {
    FrontSpec f;
    f.kind = kind;
    f.style = FrontStyle::FramePanel;
    f.frame.joint = joint;
    if (kind == FrontKind::DrawerStack) f.drawerHeights = {SectionSize::weight(1)};
    return f;
}
const Groove* groove(const domain::Part& p, const std::string& suffix) {
    for (const auto& op : p.operations)
        if (op.id.str() == p.id.str() + "/op." + suffix) return std::get_if<Groove>(&op.shape);
    return nullptr;
}
geometry::Vec3 at(const domain::Part& p, const Hole& h) { return machining::faceToAssembly(p, h.face, h.u, h.v); }
} // namespace

TEST(FramePanel, StubTenonDoorHasFivePiecesGroovesTenonsHingesOnStileAndHandle) {
    CabinetDefinition d = defaultCabinet();
    d.interior.front = framed(FrontKind::Door);
    const GenerationResult r = gen(d);
    const domain::Assembly& a = r.assembly;
    EXPECT_FALSE(has(a, "door"));
    // Stiles: full height 716, 70 wide: left x 2..72, right x 598 - 70 = 528..598.
    expectSize(part(a, "door.stile.left"), 716, 70, 18);
    expectBox(part(a, "door.stile.left"), {2, -20, 2}, {72, -2, 718});
    expectBox(part(a, "door.stile.right"), {528, -20, 2}, {598, -2, 718});
    // Rails: 596 - 2 * 70 = 456 between the stiles + 2 tenons of 10 = 476, x 62..538.
    expectSize(part(a, "door.rail.top"), 476, 70, 18);
    expectBox(part(a, "door.rail.top"), {62, -20, 648}, {538, -2, 718});
    expectBox(part(a, "door.rail.bottom"), {62, -20, 2}, {538, -2, 72});
    // Panel: opening 456 x 576 + 2 * (10 - 2) = 472 x 592, centred in the thickness:
    // y = -2 - 9 - 4 = -15 .. -7; x 72 - 8 = 64 .. 536, z 64 .. 656.
    expectSize(part(a, "door.panel"), 592, 472, 8);
    expectBox(part(a, "door.panel"), {64, -15, 64}, {536, -7, 656});
    for (const char* s : {"door.stile.left", "door.stile.right", "door.rail.top", "door.rail.bottom", "door.panel"})
        EXPECT_EQ(part(a, s).role, domain::PartRole::Door) << s;
    EXPECT_EQ(part(a, "door.rail.top").name, "Door top rail");
    EXPECT_EQ(part(a, "door.panel").custom.at("furniture.front_piece"), "panel");
    // Grooves 8 wide (panel + groove_fit 0), 10 deep, centred at v = 9 of the inner edges;
    // stiles grooved through (the tenons plug the ends).
    EXPECT_EQ(*groove(part(a, "door.stile.left"), "groove.panel"), (Groove{Face::L2, 0, 9, 716, 9, 8, 10}));
    EXPECT_EQ(*groove(part(a, "door.stile.right"), "groove.panel"), (Groove{Face::L1, 0, 9, 716, 9, 8, 10}));
    EXPECT_EQ(*groove(part(a, "door.rail.top"), "groove.panel"), (Groove{Face::L2, 0, 9, 476, 9, 8, 10}));
    EXPECT_EQ(*groove(part(a, "door.rail.bottom"), "groove.panel"), (Groove{Face::L1, 0, 9, 476, 9, 8, 10}));
    // Tenon cheeks: (18 - 8) / 2 = 5 deep, 10 long, across the full rail width on A and B.
    EXPECT_EQ(*groove(part(a, "door.rail.top"), "tenon.left.a"), (Groove{Face::A, 5, 0, 5, 70, 10, 5}));
    EXPECT_EQ(*groove(part(a, "door.rail.top"), "tenon.right.b"), (Groove{Face::B, 471, 0, 471, 70, 10, 5}));
    // Hinge cups in the left (hinge-side) stile: 716 mm -> 2 hinges at 100 / 616, cup 21.5 from the edge.
    EXPECT_THAT(holes(a, "door.stile.left", "hw.hinges.door"),
                ElementsAre(Hole{Face::A, 100, 21.5, 35, 13, false}, Hole{Face::A, 616, 21.5, 35, 13, false}));
    // Vertical handle on the right stile, 50 from the free edge: x = 548, z = 360 -/+ 64.
    const auto hh = holes(a, "door.stile.right", "hw.handle.door");
    ASSERT_EQ(hh.size(), 2u);
    EXPECT_TRUE(geometry::approxEqual(at(part(a, "door.stile.right"), hh[0]), {548, -2, 296}));
    EXPECT_TRUE(geometry::approxEqual(at(part(a, "door.stile.right"), hh[1]), {548, -2, 424}));
    EXPECT_THAT(errorsOf(r.diagnostics), IsEmpty());
    EXPECT_THAT(interpenetrations(a), IsEmpty());
    // The panel takes the 18 mm front board unless a panel material is chosen: warned.
    EXPECT_TRUE(hasCode(r.diagnostics, "furniture.front.panel_thickness"));
}

TEST(FramePanel, DowelledDrawerFrontHasDowelsStoppedGroovesAndRailHandle) {
    CabinetDefinition d = defaultCabinet();
    d.interior.front = framed(FrontKind::DrawerStack, FrameJoint::Dowel);
    const GenerationResult r = gen(d);
    const domain::Assembly& a = r.assembly;
    // Rails without tenons: 456 long, x 72..528. Panel in drawer orientation: 472 long, 592 wide.
    expectBox(part(a, "drawer.1.rail.top"), {72, -20, 648}, {528, -2, 718});
    expectSize(part(a, "drawer.1.panel"), 472, 592, 8);
    EXPECT_EQ(part(a, "drawer.1.stile.left").role, domain::PartRole::DrawerFront);
    // Stile grooves stop groove_depth beyond the opening: 70 - 10 = 60 .. 716 - 60 = 656.
    EXPECT_EQ(*groove(part(a, "drawer.1.stile.left"), "groove.panel"), (Groove{Face::L2, 60, 9, 656, 9, 8, 10}));
    EXPECT_EQ(groove(part(a, "drawer.1.rail.top"), "tenon.left.a"), nullptr);
    // Dowels: solid rail width 70 - 10 = 60, at 15 and 45 from the outer edge; holes 30 / 2 + 1 = 16 deep.
    const domain::HardwareInstance* bl = instance(a, "hw.frame_dowels.drawer.1.bottom_left");
    ASSERT_NE(bl, nullptr);
    EXPECT_EQ(bl->quantity, 2);
    EXPECT_EQ(bl->definition.str(), hardware::ids::kDowel8x30);
    EXPECT_THAT(holes(a, "drawer.1.stile.left", "hw.frame_dowels.drawer.1.bottom_left"),
                ElementsAre(Hole{Face::L2, 15, 9, 8, 16, false}, Hole{Face::L2, 45, 9, 8, 16, false}));
    // Bottom rail: local y runs down from its upper (inner) edge: 70 - 15 = 55, 70 - 45 = 25.
    EXPECT_THAT(holes(a, "drawer.1.rail.bottom", "hw.frame_dowels.drawer.1.bottom_left"),
                ElementsAre(Hole{Face::W1, 55, 9, 8, 16, false}, Hole{Face::W1, 25, 9, 8, 16, false}));
    EXPECT_THAT(holes(a, "drawer.1.stile.right", "hw.frame_dowels.drawer.1.top_right"),
                ElementsAre(Hole{Face::L1, 701, 9, 8, 16, false}, Hole{Face::L1, 671, 9, 8, 16, false}));
    // Coaxial: stile hole 1 and rail hole 1 of the bottom-left joint at z = 2 + 15 = 17.
    const auto sh = holes(a, "drawer.1.stile.left", "hw.frame_dowels.drawer.1.bottom_left");
    const auto rh = holes(a, "drawer.1.rail.bottom", "hw.frame_dowels.drawer.1.bottom_left");
    EXPECT_TRUE(geometry::approxEqual(at(part(a, "drawer.1.stile.left"), sh[0]), {72, -11, 17}));
    EXPECT_TRUE(geometry::approxEqual(at(part(a, "drawer.1.rail.bottom"), rh[0]), {72, -11, 17}));
    // Center handle -> top rail, centred: x = 300 -/+ 64, z = 718 - 35 = 683.
    const auto hh = holes(a, "drawer.1.rail.top", "hw.handle.drawer.1");
    ASSERT_EQ(hh.size(), 2u);
    EXPECT_TRUE(geometry::approxEqual(at(part(a, "drawer.1.rail.top"), hh[0]), {236, -2, 683}));
    EXPECT_TRUE(geometry::approxEqual(at(part(a, "drawer.1.rail.top"), hh[1]), {364, -2, 683}));
    EXPECT_THAT(errorsOf(r.diagnostics), IsEmpty());
    EXPECT_THAT(interpenetrations(a), IsEmpty());
}

TEST(FramePanel, PanelMaterialOfMatchingThicknessIsUsedWithoutWarning) {
    CabinetDefinition d = defaultCabinet();
    d.interior.front = framed(FrontKind::DoorPair);
    d.interior.front->frame.panelThickness = 3;
    d.interior.front->frame.panelMaterial = domain::MaterialId("mat.hdf3"); // fixture: 3 mm HDF
    const GenerationResult r = gen(d);
    EXPECT_EQ(part(r.assembly, "door.left.panel").material.str(), "mat.hdf3");
    EXPECT_EQ(part(r.assembly, "door.right.stile.left").material, part(r.assembly, "side.left").material);
    EXPECT_FALSE(hasCode(r.diagnostics, "furniture.front.panel_thickness"));
    EXPECT_THAT(errorsOf(r.diagnostics), IsEmpty());
    EXPECT_THAT(interpenetrations(r.assembly), IsEmpty());
}

TEST(FramePanel, FlapHingesOnTopRailAndLiftsOnStiles) {
    // Wall cabinet 360 x 320 (the generic lift covers cabinets up to 600 mm): flap z 2..358.
    CabinetDefinition d = defaultCabinet();
    parametric::Transaction tx;
    tx.setValue("height", 360).setValue("depth", 320);
    ASSERT_TRUE(d.parameters.apply(tx).ok());
    d.interior.front = framed(FrontKind::Flap);
    const GenerationResult r = gen(d);
    EXPECT_FALSE(holes(r.assembly, "flap.rail.top", "hw.hinges.flap").empty());
    EXPECT_FALSE(holes(r.assembly, "flap.stile.left", "hw.lift.flap.left").empty());
    EXPECT_FALSE(holes(r.assembly, "flap.stile.right", "hw.lift.flap.right").empty());
    EXPECT_EQ(holes(r.assembly, "flap.rail.bottom", "hw.handle.flap").size(), 2u); // Center -> bottom rail
    EXPECT_THAT(errorsOf(r.diagnostics), IsEmpty());
    EXPECT_THAT(interpenetrations(r.assembly), IsEmpty());
}

TEST(FramePanel, RefusesTooSmallUnsupportedAndContouredFronts) {
    CabinetDefinition d = defaultCabinet();
    d.interior.front = framed(FrontKind::Door);
    d.interior.front->frame.stileWidth = 290; // opening 596 - 580 = 16 mm < 50 mm
    auto r = generate(d, libs());
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, core::ErrorCode::ConstraintViolation);
    EXPECT_THAT(r.error().message, HasSubstr("panel opening would be 16 x 576 mm"));

    d.interior.front = framed(FrontKind::SlidingDoors);
    r = generate(d, libs());
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, core::ErrorCode::Unsupported);
    EXPECT_THAT(r.error().message, HasSubstr("SlidingDoors"));

    d.interior.front = framed(FrontKind::Door);
    d.materials.overrides["door"].contour = ContourSpec{};
    r = generate(d, libs());
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, core::ErrorCode::Unsupported);
    EXPECT_THAT(r.error().message, HasSubstr("contour"));
    d.materials.overrides.clear();

    // Groove 16 wide in an 18 mm frame leaves 1 mm walls (< 2 mm).
    d.interior.front->frame.panelThickness = 16;
    r = generate(d, libs());
    ASSERT_FALSE(r.ok());
    EXPECT_THAT(r.error().message, HasSubstr("leaving 1 mm on each side"));

    d.interior.front->frame.panelThickness = 8;
    d.interior.front->frame.panelClearance = 10; // not less than the groove depth
    r = generate(d, libs());
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, core::ErrorCode::InvalidArgument);
}

TEST(FramePanel, HingeCupBreakingIntoTheGrooveIsAnError) {
    CabinetDefinition d = defaultCabinet();
    d.interior.front = framed(FrontKind::Door);
    // Stile 45: groove from 45 - 10 = 35; the cup reaches 21.5 + 17.5 = 39 at depth 13 (z 5..18),
    // the groove spans z 9 -/+ 4 -> they meet.
    d.interior.front->frame.stileWidth = 45;
    const GenerationResult r = gen(d);
    EXPECT_FALSE(messagesWithCode(r.diagnostics, "furniture.front.frame_hole_in_groove").empty());
}

TEST(FramePanel, SlabFrontIsUnchangedByTheFrameFields) {
    CabinetDefinition a = defaultCabinet(), b = defaultCabinet();
    FrontSpec f;
    a.interior.front = f;
    f.frame.stileWidth = 99; // ignored for Slab
    b.interior.front = f;
    EXPECT_EQ(dump(gen(a).assembly), dump(gen(b).assembly));
}

} // namespace fos::furniture::test
