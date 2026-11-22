// SPDX-License-Identifier: AGPL-3.0-only
// Cranked (half overlay / inset) hinges, top-hinged flap hinges and the generic lift mechanism.
#include "fos/hardware/hinges.hpp"
#include "fos/hardware/lift.hpp"
#include "fos/machining/validation.hpp"
#include "test_util.hpp"

using namespace fos;
using namespace fos::hardware;
using namespace fos::hardware::testing;
using domain::Face;
using domain::Hole;
using geometry::Transform;

namespace {
const geometry::Vec3 X{1, 0, 0}, Y{0, 1, 0}, Z{0, 0, 1};
/// Door / flap: length -> +Z, width -> +X, thickness -> +Y; face A (local z = t) at y = y0 + t.
domain::Part front(const char* id, const char* name, double x0, double y0, double z0, double h, double w) {
    return makePart(id, name, h, w, 18, Transform::fromAxes(Z, X, {x0, y0, z0}));
}
HingeConfig hcfg() {
    HingeConfig c;
    c.instanceId = domain::HardwareInstanceId("cab1/hw.hinges");
    return c;
}
LiftConfig lcfg(const char* id = "w/hw.lift") {
    LiftConfig c;
    c.instanceId = domain::HardwareInstanceId(id);
    return c;
}
/// 600 x H x D wall cabinet: left side x 0..18 (inner face B, u = z, v = y), right side
/// x 582..600 (inner face A, u = z, v = y), top x 18..582, z H-18..H (underside = face B,
/// u = x - 18, v = y). Flap full overlay 16: x 2..598, z 2..H-2, y -20..-2 (face A inner,
/// u = z - 2, v = x - 2).
struct Wall {
    double h, dep;
    domain::Part left, right, top, flap;
    Wall(double height = 360, double depth = 320)
        : h(height), dep(depth),
          left(makePart("w/side.left", "Left side", height, depth, 18, Transform::fromAxes(Z, Y, {18, 0, 0}))),
          right(makePart("w/side.right", "Right side", height, depth, 18, Transform::fromAxes(Z, Y, {600, 0, 0}))),
          top(makePart("w/top", "Top", 564, depth, 18, Transform::fromAxes(X, Y, {18, 0, height - 18}))),
          flap(front("w/flap", "Flap", 2, -20, 2, height - 4, 596)) {}
};
} // namespace

// Inset door x 20..416, y 0..18 (flush), z 20..700 (680 high) between the fixture sides.
// Overlay on the left side = 18 - 20 = -2 (inside 2 mm) -> within the inset range -4..0.
// 680 <= 900 -> 2 hinges at 100 / 580 from the bottom -> z 120 / 600; cups face A (100, 21.5),
// (580, 21.5). INSET plate line = recess 0 + door thickness 18 + 37 = 55 behind the side's
// front edge (y = 0) -> left side face B v = 55, u = z -/+ 16 = 104, 136, 584, 616.
TEST(CrankHinges, InsetDoorPlatePositionsByHand) {
    Cabinet c;
    const auto d = front("cab1/door", "Door", 20, 0, 20, 680, 396);
    auto r = placeHinges(d, c.left, def(ids::kHingeCup35Inset), hcfg());
    ASSERT_TRUE(r.ok()) << r.error().message;
    EXPECT_EQ(r.value().instances[0].definition.str(), "hw.hinge_cup_35_inset");
    const auto& cups = *r.value().operationsFor(d.id);
    const auto& plates = *r.value().operationsFor(c.left.id);
    ASSERT_EQ(cups.size(), 2u);
    ASSERT_EQ(plates.size(), 4u);
    expectHole(cups[0], Hole{Face::A, 100, 21.5, 35, 13, false});
    expectHole(cups[1], Hole{Face::A, 580, 21.5, 35, 13, false});
    expectHole(plates[0], Hole{Face::B, 104, 55, 5, 13, false});
    expectHole(plates[1], Hole{Face::B, 136, 55, 5, 13, false});
    expectHole(plates[2], Hole{Face::B, 584, 55, 5, 13, false});
    expectHole(plates[3], Hole{Face::B, 616, 55, 5, 13, false});
    expectVec(machining::holeAxisInAssembly(c.left, hole(plates[0])).start, {18, 55, 104});

    // Recessed 2 mm (y 2..20): plate line 2 + 18 + 37 = 57.
    const auto rec = front("cab1/door", "Door", 20, 2, 20, 680, 396);
    r = placeHinges(rec, c.left, def(ids::kHingeCup35Inset), hcfg());
    ASSERT_TRUE(r.ok()) << r.error().message;
    expectHole(r.value().operationsFor(c.left.id)->at(0), Hole{Face::B, 104, 57, 5, 13, false});
    // Recessed 8 mm > maxFrontGap 5.
    r = placeHinges(front("cab1/door", "Door", 20, 8, 20, 680, 396), c.left, def(ids::kHingeCup35Inset), hcfg());
    ASSERT_FALSE(r.ok());
    EXPECT_NE(r.error().message.find("8 mm behind the front edge"), std::string::npos) << r.error().message;
    // Legacy 0 mm-crank hinge refuses the inset door; the half-overlay hinge names its range.
    r = placeHinges(d, c.left, def(ids::kHingeCup35), hcfg());
    ASSERT_FALSE(r.ok());
    EXPECT_NE(r.error().message.find("inset hinge"), std::string::npos) << r.error().message;
    r = placeHinges(d, c.left, def(ids::kHingeCup35Half), hcfg());
    ASSERT_FALSE(r.ok());
    EXPECT_NE(r.error().message.find("by -2 mm"), std::string::npos) << r.error().message;
    EXPECT_NE(r.error().message.find("4 .. 10 mm"), std::string::npos) << r.error().message;
}

// Half-overlay door x 9..405, y -20..-2, z 2..718: overlay 18 - 9 = 9 (range 4..10). Overlay
// door -> plate line stays 37 behind the front edge; hinges at z 102 / 618 -> u 86, 118, 602, 634.
TEST(CrankHinges, HalfOverlayDoor) {
    Cabinet c;
    const auto d = front("cab1/door", "Door", 9, -20, 2, 716, 396);
    auto r = placeHinges(d, c.left, def(ids::kHingeCup35Half), hcfg());
    ASSERT_TRUE(r.ok()) << r.error().message;
    const auto& plates = *r.value().operationsFor(c.left.id);
    ASSERT_EQ(plates.size(), 4u);
    expectHole(plates[0], Hole{Face::B, 86, 37, 5, 13, false});
    expectHole(plates[3], Hole{Face::B, 634, 37, 5, 13, false});
    expectHole(r.value().operationsFor(d.id)->at(0), Hole{Face::A, 100, 21.5, 35, 13, false});
    // A full-overlay door (overlay 16) is outside the half-overlay range.
    r = placeHinges(front("cab1/door", "Door", 2, -20, 2, 716, 396), c.left, def(ids::kHingeCup35Half), hcfg());
    ASSERT_FALSE(r.ok());
    EXPECT_NE(r.error().message.find("by 16 mm"), std::string::npos) << r.error().message;
}

// Flap on a 600 x 360 wall cabinet, hinge axis X, hung on the top (inner face = underside z 342).
// Overlay = 358 - 342 = 16 (0 mm-crank hinge). Width 596 <= 900 -> 2 hinges, 100 / 496 from the
// left end x = 2 -> x 102 / 498. Cup 21.5 below the top edge: z 336.5 -> flap u = 334.5,
// v = 100 / 496. Plates on the top's underside 37 behind its front edge, x 102 -/+ 16 ->
// top u = x - 18 = 68, 100, 464, 496, v = 37.
TEST(FlapHinges, CupsAlongTheTopEdgePlatesUnderTheTop) {
    Wall w;
    auto cfg = hcfg();
    cfg.axis = X;
    const auto r = placeHinges(w.flap, w.top, def(ids::kHingeCup35), cfg);
    ASSERT_TRUE(r.ok()) << r.error().message;
    const auto& cups = *r.value().operationsFor(w.flap.id);
    const auto& plates = *r.value().operationsFor(w.top.id);
    ASSERT_EQ(cups.size(), 2u);
    ASSERT_EQ(plates.size(), 4u);
    expectHole(cups[0], Hole{Face::A, 334.5, 100, 35, 13, false});
    expectHole(cups[1], Hole{Face::A, 334.5, 496, 35, 13, false});
    expectHole(plates[0], Hole{Face::B, 68, 37, 5, 13, false});
    expectHole(plates[1], Hole{Face::B, 100, 37, 5, 13, false});
    expectHole(plates[2], Hole{Face::B, 464, 37, 5, 13, false});
    expectHole(plates[3], Hole{Face::B, 496, 37, 5, 13, false});
    expectVec(machining::holeAxisInAssembly(w.top, hole(plates[0])).start, {86, 37, 342});
    // A non-axis hinge axis is refused.
    cfg.axis = {0.6, 0.8, 0};
    EXPECT_FALSE(placeHinges(w.flap, w.top, def(ids::kHingeCup35), cfg).ok());
}

// Lift on the same cabinet. Side height 360 (240..600), depth 320 (>= 250). Side holes 40 below
// the top's underside (z 302) at y 37 / 69; flap holes 50 in from the flap edge at that side,
// 100 / 132 below the top edge z 358 -> z 258 / 226 -> u 256 / 224; left v = 52 - 2 = 50,
// right x = 598 - 50 = 548 -> v = 546.
TEST(Lift, HolesOnBothSidesAndTheFlap) {
    Wall w;
    auto r = placeLiftMechanism(w.flap, w.left, w.top, def(ids::kLiftGeneric), lcfg());
    ASSERT_TRUE(r.ok()) << r.error().message;
    ASSERT_EQ(r.value().operations.size(), 2u);
    EXPECT_EQ(r.value().operations[0].part, w.flap.id);
    const auto& fl = *r.value().operationsFor(w.flap.id);
    const auto& sl = *r.value().operationsFor(w.left.id);
    expectHole(fl[0], Hole{Face::A, 256, 50, 4, 10, false});
    expectHole(fl[1], Hole{Face::A, 224, 50, 4, 10, false});
    expectHole(sl[0], Hole{Face::B, 302, 37, 5, 12, false});
    expectHole(sl[1], Hole{Face::B, 302, 69, 5, 12, false});
    EXPECT_EQ(sl[1].id.str(), "w/hw.lift/op.4");
    expectVec(r.value().instances[0].position, {18, 37, 302});

    r = placeLiftMechanism(w.flap, w.right, w.top, def(ids::kLiftGeneric), lcfg());
    ASSERT_TRUE(r.ok()) << r.error().message;
    expectHole(r.value().operationsFor(w.flap.id)->at(0), Hole{Face::A, 256, 546, 4, 10, false});
    const auto& sr = *r.value().operationsFor(w.right.id);
    expectHole(sr[0], Hole{Face::A, 302, 37, 5, 12, false});
    expectVec(machining::holeAxisInAssembly(w.right, hole(sr[1])).start, {582, 69, 302});
    // Power factor 356 x 3 kg = 1068 is inside 400..1800; 10 kg -> 3560 is not.
    auto c = lcfg();
    c.flapWeightKg = 3;
    EXPECT_TRUE(placeLiftMechanism(w.flap, w.left, w.top, def(ids::kLiftGeneric), c).ok());
    c.flapWeightKg = 10;
    r = placeLiftMechanism(w.flap, w.left, w.top, def(ids::kLiftGeneric), c);
    ASSERT_FALSE(r.ok());
    EXPECT_NE(r.error().message.find("3560"), std::string::npos) << r.error().message;
}

TEST(Lift, SpecificErrors) {
    const auto lift = def(ids::kLiftGeneric);
    Wall low(200);
    auto r = placeLiftMechanism(low.flap, low.left, low.top, lift, lcfg());
    ASSERT_FALSE(r.ok());
    EXPECT_NE(r.error().message.find("too low"), std::string::npos) << r.error().message;
    EXPECT_NE(r.error().message.find("200 mm tall"), std::string::npos) << r.error().message;
    EXPECT_NE(r.error().message.find("240 .. 600 mm"), std::string::npos) << r.error().message;
    Wall shallow(360, 200);
    r = placeLiftMechanism(shallow.flap, shallow.left, shallow.top, lift, lcfg());
    ASSERT_FALSE(r.ok());
    EXPECT_NE(r.error().message.find("too shallow"), std::string::npos) << r.error().message;
    EXPECT_NE(r.error().message.find("200 mm deep"), std::string::npos) << r.error().message;
    Cabinet tall; // 720 mm side
    r = placeLiftMechanism(Wall().flap, tall.left, tall.top, lift, lcfg());
    ASSERT_FALSE(r.ok());
    EXPECT_NE(r.error().message.find("too tall"), std::string::npos) << r.error().message;
    // Wrong kind.
    Wall w;
    EXPECT_FALSE(placeLiftMechanism(w.flap, w.left, w.top, def(ids::kHingeCup35), lcfg()).ok());
}
