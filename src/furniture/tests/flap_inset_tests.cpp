// SPDX-License-Identifier: AGPL-3.0-only
// Inset doors with inset-crank hinges and lift-up flaps (FrontKind::Flap).
#include "fos/machining/face.hpp"
#include "fos/machining/validation.hpp"
#include "test_support.hpp"

namespace fos::furniture::test {
using domain::Face;
using domain::Hole;
using ::testing::ElementsAre;
using ::testing::HasSubstr;
using ::testing::IsEmpty;

namespace {
FrontSpec spec(FrontKind kind, OverlayKind overlay = OverlayKind::Full) {
    FrontSpec f;
    f.kind = kind;
    f.overlay = overlay;
    return f;
}
std::vector<geometry::Vec3> inAssembly(const domain::Assembly& a, const std::string& slot, const std::string& inst) {
    std::vector<geometry::Vec3> out;
    for (const auto& h : holes(a, slot, inst)) out.push_back(machining::faceToAssembly(part(a, slot), h.face, h.u, h.v));
    return out;
}
void expectPoints(const std::vector<geometry::Vec3>& got, const std::vector<geometry::Vec3>& want) {
    ASSERT_EQ(got.size(), want.size());
    for (std::size_t i = 0; i < got.size(); ++i)
        EXPECT_TRUE(geometry::approxEqual(got[i], want[i], 1e-9))
            << i << ": (" << got[i].x << ", " << got[i].y << ", " << got[i].z << ")";
}
CabinetDefinition wallCabinet(double height) {
    CabinetDefinition d = defaultCabinet();
    parametric::Transaction tx;
    tx.setValue("height", height).setValue("depth", 320);
    EXPECT_TRUE(d.parameters.apply(tx).ok());
    d.interior.front = spec(FrontKind::Flap);
    return d;
}
} // namespace

// Default cabinet 600 x 720, inset door x 20..580, y 0..18, z 20..700 (680 high), hinged left.
// Overlay on the left side = 18 - 20 = -2 -> inset hinge (16 mm crank). 2 hinges at 100 / 580
// from the door bottom -> z 120 / 600. Plate line = door thickness 18 + 37 = 55 behind the
// front edge: left side face B (u = z, v = y) at u 104, 136, 584, 616, v 55.
TEST(InsetDoor, GetsInsetCrankHingesWithPlatesBehindTheDoor) {
    CabinetDefinition d = defaultCabinet();
    d.interior.front = spec(FrontKind::Door, OverlayKind::Inset);
    const GenerationResult r = gen(d);
    const domain::Assembly& a = r.assembly;
    EXPECT_FALSE(hasCode(r.diagnostics, "furniture.front.inset_hinges"));
    ASSERT_NE(instance(a, "hw.hinges.door"), nullptr);
    EXPECT_EQ(instance(a, "hw.hinges.door")->definition.str(), hardware::ids::kHingeCup35Inset);
    EXPECT_THAT(holes(a, "side.left", "hw.hinges.door"),
                ElementsAre(Hole{Face::B, 104, 55, 5, 13, false}, Hole{Face::B, 136, 55, 5, 13, false},
                            Hole{Face::B, 584, 55, 5, 13, false}, Hole{Face::B, 616, 55, 5, 13, false}));
    EXPECT_THAT(holes(a, "door", "hw.hinges.door"),
                ElementsAre(Hole{Face::A, 100, 21.5, 35, 13, false}, Hole{Face::A, 580, 21.5, 35, 13, false}));
    EXPECT_THAT(errorsOf(r.diagnostics), IsEmpty());
    EXPECT_THAT(interpenetrations(a), IsEmpty());
    EXPECT_THAT(errorsOf(machining::validateOperations(part(a, "side.left"))), IsEmpty());
}

// 600 x 360 x 320 wall cabinet, full-overlay flap: x 2..598, z 2..358, y -20..-2.
// Hinges (axis X) on the top's underside z = 342: 2 hinges (width 596) at x 102 / 498; cups
// 21.5 below the flap's top edge (z 336.5) on its inner face y = -2; plate holes 37 behind the
// front edge at x 102 -/+ 16 and 498 -/+ 16.
// Lifts: side holes 40 below z 342 -> z 302, y 37 / 69, on the inner faces x 18 / 582; flap
// holes 50 in from each flap edge (x 52 / 548) at 100 / 132 below z 358 (z 258 / 226).
TEST(Flap, WallCabinetHingesAlongTheTopAndLiftsOnBothSides) {
    const GenerationResult r = gen(wallCabinet(360));
    const domain::Assembly& a = r.assembly;
    EXPECT_THAT(errorsOf(r.diagnostics), IsEmpty());
    expectBox(part(a, "flap"), {2, -20, 2}, {598, -2, 358});
    expectPoints(inAssembly(a, "flap", "hw.hinges.flap"), {{102, -2, 336.5}, {498, -2, 336.5}});
    expectPoints(inAssembly(a, "top", "hw.hinges.flap"),
                 {{86, 37, 342}, {118, 37, 342}, {482, 37, 342}, {514, 37, 342}});
    expectPoints(inAssembly(a, "side.left", "hw.lift.flap.left"), {{18, 37, 302}, {18, 69, 302}});
    expectPoints(inAssembly(a, "side.right", "hw.lift.flap.right"), {{582, 37, 302}, {582, 69, 302}});
    expectPoints(inAssembly(a, "flap", "hw.lift.flap.left"), {{52, -2, 258}, {52, -2, 226}});
    expectPoints(inAssembly(a, "flap", "hw.lift.flap.right"), {{548, -2, 258}, {548, -2, 226}});
    ASSERT_NE(instance(a, "hw.lift.flap.left"), nullptr);
    EXPECT_EQ(instance(a, "hw.lift.flap.left")->definition.str(), hardware::ids::kLiftGeneric);
    EXPECT_THAT(interpenetrations(a), IsEmpty());
    for (const char* slot : {"flap", "top", "side.left", "side.right"})
        EXPECT_THAT(errorsOf(machining::validateOperations(part(a, slot))), IsEmpty()) << slot;
}

TEST(Flap, TooLowCabinetIsASpecificError) {
    const GenerationResult r = gen(wallCabinet(200));
    const auto msgs = messagesWithCode(r.diagnostics, "furniture.hardware.placement_failed");
    ASSERT_EQ(msgs.size(), 2u); // one per lift mechanism
    EXPECT_THAT(msgs[0], HasSubstr("too low"));
    EXPECT_THAT(msgs[0], HasSubstr("200 mm tall"));
    EXPECT_THAT(msgs[0], HasSubstr("240 .. 600 mm"));
}

TEST(Flap, InsetFlapUsesInsetHingeAndRailsAreRefused) {
    CabinetDefinition d = wallCabinet(360);
    d.interior.front->overlay = OverlayKind::Inset;
    GenerationResult r = gen(d);
    EXPECT_THAT(errorsOf(r.diagnostics), IsEmpty());
    // Inset flap x 20..580, y 0..18, top edge z 340 (overlay -2): hinges 100 / 460 from x 20 ->
    // x 120 / 480; plates 18 + 37 = 55 behind the front edge at x 104, 136, 464, 496.
    ASSERT_NE(instance(r.assembly, "hw.hinges.flap"), nullptr);
    EXPECT_EQ(instance(r.assembly, "hw.hinges.flap")->definition.str(), hardware::ids::kHingeCup35Inset);
    expectPoints(inAssembly(r.assembly, "top", "hw.hinges.flap"),
                 {{104, 55, 342}, {136, 55, 342}, {464, 55, 342}, {496, 55, 342}});

    ConstructionOptions o;
    o.top = TopKind::Rails;
    CabinetDefinition rails = defaultCabinet(o);
    rails.interior.front = spec(FrontKind::Flap);
    const auto bad = generate(rails, libs());
    ASSERT_FALSE(bad.ok());
    EXPECT_THAT(bad.error().message, HasSubstr("stretcher rails"));
}

} // namespace fos::furniture::test
