// SPDX-License-Identifier: AGPL-3.0-only
// Glass-panel frame fronts (README "Frame-and-panel fronts", FramePanelKind::Glass). Default
// cabinet: 600 x 720 x 560, 18 mm fronts; a full-overlay root door spans x 2..598 (596 wide),
// z 2..718 (716 high), inner face A at y = -2, outer face at y = -20. Frame defaults: stiles /
// rails 70, clearance 2; glass defaults: 4 mm glass, rebate 10 wide x 10 deep, bead strips.
#include "test_support.hpp"

namespace fos::furniture::test {
using domain::Face;
using domain::Groove;
using ::testing::HasSubstr;
using ::testing::IsEmpty;

namespace {
FrontSpec glassDoor() {
    FrontSpec f;
    f.kind = FrontKind::Door;
    f.style = FrontStyle::FramePanel;
    f.frame.joint = FrameJoint::Dowel;
    f.frame.panelKind = FramePanelKind::Glass;
    return f;
}
/// Fixture libraries plus a 4 mm glass material "mat.glass4" of kind Glass.
Libraries glassLibs() {
    Libraries l = libs();
    domain::Material glass = l.materials[0];
    glass.id = domain::MaterialId("mat.glass4");
    glass.name = "Clear glass 4 mm";
    glass.kind = domain::MaterialKind::Glass;
    glass.thickness = 4.0;
    glass.hasGrain = false;
    l.materials.push_back(glass);
    return l;
}
GenerationResult genGlass(const CabinetDefinition& d) {
    auto r = generate(d, glassLibs());
    EXPECT_TRUE(r.ok()) << (r.ok() ? "" : r.error().message);
    return r.ok() ? r.value() : GenerationResult{};
}
const Groove* groove(const domain::Part& p, const std::string& suffix) {
    for (const auto& op : p.operations)
        if (op.id.str() == p.id.str() + "/op." + suffix) return std::get_if<Groove>(&op.shape);
    return nullptr;
}
core::Result<GenerationResult> attempt(const FrontSpec& f) {
    CabinetDefinition d = defaultCabinet();
    d.interior.front = f;
    return generate(d, glassLibs());
}
} // namespace

TEST(GlassPanel, DoorHasRebatesGlassAndBeadsWithoutInterpenetration) {
    CabinetDefinition d = defaultCabinet();
    d.interior.front = glassDoor();
    d.interior.front->frame.panelMaterial = domain::MaterialId("mat.glass4");
    const GenerationResult r = genGlass(d);
    const domain::Assembly& a = r.assembly;
    // Frame as the dowelled frame: stiles x 2..72 / 528..598, rails 456 long between them.
    expectBox(part(a, "door.stile.left"), {2, -20, 2}, {72, -2, 718});
    expectBox(part(a, "door.rail.top"), {72, -20, 648}, {528, -2, 718});
    // Glass: opening 456 x 576 + 2 * (10 - 2) = 472 x 592, 4 thick, on the rebate shoulder
    // y = -2 - 10 = -12 .. -8; x 72 - 8 = 64 .. 536, z 64 .. 656.
    const domain::Part g = part(a, "door.panel");
    expectSize(g, 592, 472, 4);
    expectBox(g, {64, -12, 64}, {536, -8, 656});
    EXPECT_EQ(g.name, "Door glass");
    EXPECT_EQ(g.material.str(), "mat.glass4");
    EXPECT_EQ(g.grain, domain::GrainDirection::None);
    EXPECT_EQ(g.custom.at("furniture.glass"), "true");
    EXPECT_EQ(g.custom.at("furniture.front_piece"), "panel");
    EXPECT_EQ(part(a, "door.stile.left").custom.count("furniture.glass"), 0u);
    // Rebates: 10 deep into the inner edge, band v = 18 - 10 .. 18 (centre 13, width 10) at
    // the back face A; stiles stop 10 beyond the opening: 70 - 10 = 60 .. 716 - 60 = 656.
    EXPECT_EQ(*groove(part(a, "door.stile.left"), "rebate.glass"), (Groove{Face::L2, 60, 13, 656, 13, 10, 10}));
    EXPECT_EQ(*groove(part(a, "door.stile.right"), "rebate.glass"), (Groove{Face::L1, 60, 13, 656, 13, 10, 10}));
    EXPECT_EQ(*groove(part(a, "door.rail.top"), "rebate.glass"), (Groove{Face::L2, 0, 13, 456, 13, 10, 10}));
    EXPECT_EQ(*groove(part(a, "door.rail.bottom"), "rebate.glass"), (Groove{Face::L1, 0, 13, 456, 13, 10, 10}));
    EXPECT_EQ(groove(part(a, "door.stile.left"), "groove.panel"), nullptr);
    // Beads: 10 wide, 10 - 4 = 6 thick, y -8..-2. Top / bottom 456 + 2 * 10 = 476 long
    // (x 62..538), sides 716 - 140 = 576 long between them (z 72..648).
    expectSize(part(a, "door.bead.top"), 476, 10, 6);
    expectBox(part(a, "door.bead.top"), {62, -8, 648}, {538, -2, 658});
    expectBox(part(a, "door.bead.bottom"), {62, -8, 62}, {538, -2, 72});
    expectSize(part(a, "door.bead.left"), 576, 10, 6);
    expectBox(part(a, "door.bead.left"), {62, -8, 72}, {72, -2, 648});
    expectBox(part(a, "door.bead.right"), {528, -8, 72}, {538, -2, 648});
    EXPECT_EQ(part(a, "door.bead.left").custom.at("furniture.front_piece"), "bead");
    EXPECT_EQ(part(a, "door.bead.left").name, "Door left glazing bead");
    // Dowels: solid rail width 70 - 10 = 60 -> same holes as the grooved dowelled frame.
    EXPECT_FALSE(holes(a, "door.stile.left", "hw.frame_dowels.door.bottom_left").empty());
    EXPECT_FALSE(holes(a, "door.stile.left", "hw.hinges.door").empty());
    EXPECT_THAT(errorsOf(r.diagnostics), IsEmpty());
    EXPECT_FALSE(hasCode(r.diagnostics, "furniture.front.glass_material"));
    EXPECT_FALSE(hasCode(r.diagnostics, "furniture.front.panel_thickness"));
    EXPECT_THAT(interpenetrations(a), IsEmpty());
}

TEST(GlassPanel, NonGlassMaterialWarnsAndBeadsNoneAddsNoBeads) {
    CabinetDefinition d = defaultCabinet();
    d.interior.front = glassDoor();
    d.interior.front->frame.glass.beads = GlassBeads::None;
    const GenerationResult r = genGlass(d); // panel falls back to the 18 mm particleboard front
    EXPECT_TRUE(hasCode(r.diagnostics, "furniture.front.glass_material"));
    EXPECT_THAT(messagesWithCode(r.diagnostics, "furniture.front.glass_material").front(), HasSubstr("Particleboard"));
    EXPECT_TRUE(hasCode(r.diagnostics, "furniture.front.panel_thickness"));
    EXPECT_FALSE(has(r.assembly, "door.bead.top"));
    EXPECT_EQ(part(r.assembly, "door.panel").custom.at("furniture.glass"), "true");
    EXPECT_THAT(errorsOf(r.diagnostics), IsEmpty());
    EXPECT_THAT(interpenetrations(r.assembly), IsEmpty());
}

TEST(GlassPanel, RefusesStubTenonsThinStilesDeepRebatesAndThinBeads) {
    FrontSpec f = glassDoor();
    f.frame.joint = FrameJoint::StubTenon;
    auto r = attempt(f);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, core::ErrorCode::Unsupported);
    EXPECT_THAT(r.error().message, HasSubstr("Use joint Dowel"));

    // Stiles 28 - rebate 10 = 18 mm of solid frame < 20 mm.
    f = glassDoor();
    f.frame.stileWidth = 28;
    r = attempt(f);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, core::ErrorCode::ConstraintViolation);
    EXPECT_THAT(r.error().message, HasSubstr("leaves 18 mm of solid frame"));

    // Rebate 17 deep in 18 mm leaves a 1 mm lip (< 2 mm).
    f = glassDoor();
    f.frame.glass.rebateDepth = 17;
    r = attempt(f);
    ASSERT_FALSE(r.ok());
    EXPECT_THAT(r.error().message, HasSubstr("leaving a 1 mm lip"));

    // Rebate 8, glass 6: 2 mm for beads (< 3 mm); without beads it is accepted.
    f = glassDoor();
    f.frame.glass.rebateDepth = 8;
    f.frame.glass.glassThickness = 6;
    r = attempt(f);
    ASSERT_FALSE(r.ok());
    EXPECT_THAT(r.error().message, HasSubstr("leaving 2 mm for the glazing beads"));
    f.frame.glass.beads = GlassBeads::None;
    EXPECT_TRUE(attempt(f).ok());

    // Glass thicker than the rebate is deep.
    f.frame.glass.rebateDepth = 4;
    r = attempt(f);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, core::ErrorCode::InvalidArgument);
    EXPECT_THAT(r.error().message, HasSubstr("stand proud"));

    f = glassDoor();
    f.frame.panelClearance = 10; // not less than the rebate width
    r = attempt(f);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, core::ErrorCode::InvalidArgument);

    f = glassDoor();
    f.frame.glass.glassThickness = 0;
    r = attempt(f);
    ASSERT_FALSE(r.ok());
    EXPECT_THAT(r.error().message, HasSubstr("glass thickness 0 mm"));
}

TEST(GlassPanel, GroovedFrontIgnoresTheGlassFields) {
    CabinetDefinition a = defaultCabinet(), b = defaultCabinet();
    FrontSpec f;
    f.style = FrontStyle::FramePanel;
    a.interior.front = f;
    f.frame.glass.glassThickness = 6; // ignored for Grooved
    f.frame.glass.beads = GlassBeads::None;
    b.interior.front = f;
    EXPECT_EQ(dump(gen(a).assembly), dump(gen(b).assembly));
    EXPECT_EQ(part(gen(a).assembly, "door.panel").custom.count("furniture.glass"), 0u);
}

} // namespace fos::furniture::test
