// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/hardware/handle.hpp"
#include "fos/hardware/hinges.hpp"
#include "fos/machining/validation.hpp"
#include "test_util.hpp"

using namespace fos;
using namespace fos::hardware;
using namespace fos::hardware::testing;
using domain::Face;
using domain::Hole;
using geometry::Transform;

namespace {
/// Overlay door for the fixture cabinet: 716 (height) x 396 (width) x `t`, standing 2 mm in
/// front of the carcass. Length -> +Z, width -> +X, thickness -> +Y (cross(Z, X) = Y).
/// With origin (x0, -2 - t, 2) it occupies x x0..x0+396, y (-2-t)..-2, z 2..718.
/// Face A (local z = t) is at y = -2: the INNER face, looking at the cabinet.
/// Door local x = assembly z - 2; door local y = assembly x - x0.
domain::Part door(double x0, double t = 18, double height = 716, double width = 396) {
    auto d = makePart("cab1/door", "Door", height, width, t, Transform::fromAxes({0, 0, 1}, {1, 0, 0}, {x0, -2 - t, 2}));
    d.role = domain::PartRole::Door;
    return d;
}
HingeConfig hingeConfig(const char* id = "cab1/hw.hinges") {
    HingeConfig c;
    c.instanceId = domain::HardwareInstanceId(id);
    return c;
}
HandleConfig handleConfig(const char* id = "cab1/hw.handle") {
    HandleConfig c;
    c.instanceId = domain::HardwareInstanceId(id);
    return c;
}
} // namespace

TEST(HingeRules, DefaultTableAndPositions) {
    const auto rules = defaultHingeCountRules();
    EXPECT_EQ(hingeCount(716, DoorWeightClass::Medium, rules).value(), 2);
    EXPECT_EQ(hingeCount(716, DoorWeightClass::Light, rules).value(), 2);
    EXPECT_EQ(hingeCount(716, DoorWeightClass::Heavy, rules).value(), 3);
    EXPECT_EQ(hingeCount(900, DoorWeightClass::Medium, rules).value(), 2); // boundary inclusive
    EXPECT_EQ(hingeCount(901, DoorWeightClass::Medium, rules).value(), 3);
    EXPECT_EQ(hingeCount(1600, DoorWeightClass::Heavy, rules).value(), 4);
    EXPECT_EQ(hingeCount(2000, DoorWeightClass::Medium, rules).value(), 4);
    EXPECT_EQ(hingeCount(2400, DoorWeightClass::Heavy, rules).value(), 6);
    const auto tooTall = hingeCount(2500, DoorWeightClass::Medium, rules);
    ASSERT_FALSE(tooTall.ok());
    EXPECT_NE(tooTall.error().message.find("2500 mm"), std::string::npos);
    EXPECT_NE(tooTall.error().message.find("2400 mm"), std::string::npos);
    EXPECT_FALSE(hingeCount(716, DoorWeightClass::Medium, {}).ok());
    // Custom table, deliberately unsorted: the tightest covering rule wins.
    const std::vector<HingeCountRule> custom{{2000, 4, 5, 6}, {1000, 2, 3, 4}};
    EXPECT_EQ(hingeCount(800, DoorWeightClass::Medium, custom).value(), 3);
    EXPECT_EQ(hingeCount(1500, DoorWeightClass::Light, custom).value(), 4);
    EXPECT_FALSE(hingeCount(800, DoorWeightClass::Medium, {{1000, 1, 1, 1}}).ok()); // fewer than 2

    EXPECT_EQ(hingePositions(716, 2, 100, 100).value(), (std::vector<double>{100, 616}));
    EXPECT_EQ(hingePositions(716, 3, 100, 100).value(), (std::vector<double>{100, 358, 616}));
    EXPECT_EQ(hingePositions(716, 4, 100, 100).value(), (std::vector<double>{100, 272, 444, 616})); // step 516/3 = 172
    EXPECT_EQ(hingePositions(716, 2, 80, 120).value(), (std::vector<double>{120, 636}));
    EXPECT_FALSE(hingePositions(716, 1, 100, 100).ok());
    EXPECT_FALSE(hingePositions(200, 2, 100, 100).ok());
}

// Left-hung door, x 2..398. Hand computation:
//   height 716 <= 900, medium -> 2 hinges, 100 mm from the bottom and top edges:
//   door-local heights 100 and 616 -> assembly z = 102 and 618.
//   Hinged edge = the door edge at the left side = x = 2 = door local y = 0 (edge L1).
//   Cup centre 21.5 from that edge -> door local (100, 21.5) and (616, 21.5) on inner face A.
//   Left side inner face B (x = 18); its front edge is y = 0; plate line 37 behind -> local y = 37;
//   side local x = assembly z -> plate holes at 102 -/+ 16 = 86, 118 and 618 -/+ 16 = 602, 634.
TEST(PlaceHinges, LeftHungDoorOnFixture) {
    Cabinet c;
    const auto d = door(2);
    const auto r = placeHinges(d, c.left, def(ids::kHingeCup35), hingeConfig());
    ASSERT_TRUE(r.ok()) << r.error().message;
    const Placement& p = r.value();
    ASSERT_EQ(p.instances.size(), 1u);
    EXPECT_EQ(p.instances[0].quantity, 2);
    EXPECT_EQ(p.instances[0].definition.str(), "hw.hinge_cup_35");
    EXPECT_EQ(p.instances[0].parts, (std::vector<domain::PartId>{d.id, c.left.id}));
    expectVec(p.instances[0].position, {23.5, -2, 102}); // lowest cup centre on the inner face

    ASSERT_EQ(p.operations.size(), 2u);
    EXPECT_EQ(p.operations[0].part, d.id);
    EXPECT_EQ(p.operations[1].part, c.left.id);
    const auto& cups = p.operations[0].operations;
    const auto& plates = p.operations[1].operations;
    ASSERT_EQ(cups.size(), 2u);
    ASSERT_EQ(plates.size(), 4u);
    expectHole(cups[0], Hole{Face::A, 100, 21.5, 35, 13, false});
    expectHole(cups[1], Hole{Face::A, 616, 21.5, 35, 13, false});
    expectHole(plates[0], Hole{Face::B, 86, 37, 5, 13, false});
    expectHole(plates[1], Hole{Face::B, 118, 37, 5, 13, false});
    expectHole(plates[2], Hole{Face::B, 602, 37, 5, 13, false});
    expectHole(plates[3], Hole{Face::B, 634, 37, 5, 13, false});
    EXPECT_EQ(cups[0].id.str(), "cab1/hw.hinges/op.1");
    EXPECT_EQ(plates[0].id.str(), "cab1/hw.hinges/op.2");
    EXPECT_EQ(plates[1].id.str(), "cab1/hw.hinges/op.3");
    EXPECT_EQ(cups[1].id.str(), "cab1/hw.hinges/op.4");
    EXPECT_EQ(plates[3].id.str(), "cab1/hw.hinges/op.6");
    for (const auto& op : plates) EXPECT_EQ(op.source, "cab1/hw.hinges");

    // Assembly frame: cup drilled from the door's inner face (y = -2) towards the outside (-Y);
    // plate holes on the side's inner face x = 18, 37 mm behind the front edge, drilled in -X.
    auto a = machining::holeAxisInAssembly(d, hole(cups[1]));
    expectVec(a.start, {23.5, -2, 618});
    expectVec(a.direction, {0, -1, 0});
    a = machining::holeAxisInAssembly(c.left, hole(plates[2]));
    expectVec(a.start, {18, 37, 602});
    expectVec(a.direction, {-1, 0, 0});

    // The generated drilling is valid on both panels.
    auto doorCopy = d;
    doorCopy.operations = cups;
    EXPECT_TRUE(machining::validateOperations(doorCopy).empty());
    ASSERT_TRUE(applyPlacement(c.a, Placement{p.instances, {p.operations[1]}}).ok());
    EXPECT_TRUE(machining::validateOperations(c.left).empty());
}

// Right-hung door, x 402..798: hinged edge at x = 798 = door local y = 396 (edge L2);
// cup centre x = 798 - 21.5 = 776.5 -> local y = 374.5. Right side inner face is A (x = 782).
TEST(PlaceHinges, RightHungDoorHeavyClass) {
    Cabinet c;
    const auto d = door(402);
    auto cfg = hingeConfig();
    cfg.weightClass = DoorWeightClass::Heavy; // 716 mm, heavy -> 3 hinges at 100 / 358 / 616
    const auto r = placeHinges(d, c.right, def(ids::kHingeCup35), cfg);
    ASSERT_TRUE(r.ok()) << r.error().message;
    EXPECT_EQ(r.value().instances[0].quantity, 3);
    const auto& cups = *r.value().operationsFor(d.id);
    const auto& plates = *r.value().operationsFor(c.right.id);
    ASSERT_EQ(cups.size(), 3u);
    ASSERT_EQ(plates.size(), 6u);
    expectHole(cups[0], Hole{Face::A, 100, 374.5, 35, 13, false});
    expectHole(cups[1], Hole{Face::A, 358, 374.5, 35, 13, false});
    expectHole(cups[2], Hole{Face::A, 616, 374.5, 35, 13, false});
    // Middle hinge: assembly z = 2 + 358 = 360 -> plate holes at 344 and 376.
    expectHole(plates[2], Hole{Face::A, 344, 37, 5, 13, false});
    expectHole(plates[3], Hole{Face::A, 376, 37, 5, 13, false});
    expectHole(plates[5], Hole{Face::A, 634, 37, 5, 13, false});
    expectVec(machining::holeAxisInAssembly(c.right, hole(plates[0])).start, {782, 37, 86});
    expectVec(machining::holeAxisInAssembly(c.right, hole(plates[0])).direction, {1, 0, 0});
    expectVec(machining::holeAxisInAssembly(d, hole(cups[0])).start, {776.5, -2, 102});
}

TEST(PlaceHinges, ConfigurableCountOffsetsAndDoorOrientation) {
    Cabinet c;
    auto cfg = hingeConfig();
    cfg.count = 4;
    cfg.offsetFromTop = 80;
    cfg.offsetFromBottom = 120; // heights 120 .. 636, step 516 / 3 = 172 -> 120, 292, 464, 636
    auto r = placeHinges(door(2), c.left, def(ids::kHingeCup35), cfg);
    ASSERT_TRUE(r.ok()) << r.error().message;
    const auto& cups = r.value().operations[0].operations;
    ASSERT_EQ(cups.size(), 4u);
    const double hs[] = {120, 292, 464, 636};
    for (int i = 0; i < 4; ++i) expectHole(cups[i], Hole{Face::A, hs[i], 21.5, 35, 13, false});

    // The same physical door modelled upside-down and with its other face inwards:
    // length -> -Z, width -> +X, thickness -> cross(-Z, X) = -Y; origin (2, -2, 718):
    // x 2..398, y -20..-2, z 2..718. Now face B (local z = 0, y = -2) is the inner face and
    // local x = 718 - z. Default config: z = 102 -> local x = 616; z = 618 -> local x = 100.
    const auto flipped = makePart("d2", "Flipped door", 716, 396, 18, Transform::fromAxes({0, 0, -1}, {1, 0, 0}, {2, -2, 718}));
    r = placeHinges(flipped, c.left, def(ids::kHingeCup35), hingeConfig());
    ASSERT_TRUE(r.ok()) << r.error().message;
    expectHole(r.value().operations[0].operations[0], Hole{Face::B, 616, 21.5, 35, 13, false}); // bottom hinge first
    expectHole(r.value().operations[0].operations[1], Hole{Face::B, 100, 21.5, 35, 13, false});
    expectHole(r.value().operations[1].operations[0], Hole{Face::B, 86, 37, 5, 13, false}); // side unchanged
}

TEST(PlaceHinges, Failures) {
    Cabinet c;
    const auto hinge = def(ids::kHingeCup35);
    // Door too thin: 15 mm - 2 mm = 13 mm <= cup depth 13 -> the cup would (nearly) break through.
    auto r = placeHinges(door(2, 15), c.left, hinge, hingeConfig());
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, core::ErrorCode::ConstraintViolation);
    EXPECT_NE(r.error().message.find("13 mm deep hinge cup"), std::string::npos) << r.error().message;
    EXPECT_NE(r.error().message.find("15 mm thick door 'Door'"), std::string::npos) << r.error().message;
    // 16 mm is fine with the default 2 mm (13 < 14) but not when 4 mm must remain.
    EXPECT_TRUE(placeHinges(door(2, 16), c.left, hinge, hingeConfig()).ok());
    auto strict = hingeConfig();
    strict.fit.minRemainingMaterial = 4;
    EXPECT_FALSE(placeHinges(door(2, 16), c.left, hinge, strict).ok());
    // Door too narrow: the cup needs 21.5 + 17.5 = 39 mm; 38 mm fails, 39 mm passes.
    r = placeHinges(door(2, 18, 716, 38), c.left, hinge, hingeConfig());
    ASSERT_FALSE(r.ok());
    EXPECT_NE(r.error().message.find("38 mm wide, too narrow"), std::string::npos) << r.error().message;
    EXPECT_NE(r.error().message.find("39 mm"), std::string::npos);
    EXPECT_TRUE(placeHinges(door(2, 18, 716, 39), c.left, hinge, hingeConfig()).ok());
    // Door 32 mm in front of the carcass (y -50..-32).
    auto far = door(2);
    far.placement.origin = {2, -50, 2};
    r = placeHinges(far, c.left, hinge, hingeConfig());
    ASSERT_FALSE(r.ok());
    EXPECT_NE(r.error().message.find("32 mm in front"), std::string::npos) << r.error().message;
    // Inset door between the sides (x 20..416, y 0..18).
    auto inset = door(20);
    inset.placement.origin = {20, 0, 2};
    r = placeHinges(inset, c.left, hinge, hingeConfig());
    ASSERT_FALSE(r.ok());
    EXPECT_NE(r.error().message.find("inset"), std::string::npos) << r.error().message;
    // The right-hand door cannot hang on the left side: its nearest edge is 384 mm inside.
    EXPECT_FALSE(placeHinges(door(402), c.left, hinge, hingeConfig()).ok());
    // A door reaching far above the side: top hinge plate at z = 2 + 898 = 900 > 720.
    r = placeHinges(door(2, 18, 998), c.left, hinge, hingeConfig());
    ASSERT_FALSE(r.ok());
    EXPECT_NE(r.error().message.find("'Left side'"), std::string::npos) << r.error().message;
    EXPECT_NE(r.error().message.find("(884, 37)"), std::string::npos) << r.error().message;
    // "Side" that is not vertical.
    r = placeHinges(door(2), c.bottom, hinge, hingeConfig());
    ASSERT_FALSE(r.ok());
    // No rule for the height.
    auto cfg = hingeConfig();
    cfg.rules = {{500, 2, 2, 2}};
    r = placeHinges(door(2), c.left, hinge, cfg);
    ASSERT_FALSE(r.ok());
    EXPECT_NE(r.error().message.find("716 mm"), std::string::npos) << r.error().message;
    // Offsets that leave no room, explicit count of 1, wrong kind, missing id.
    cfg = hingeConfig();
    cfg.offsetFromTop = cfg.offsetFromBottom = 400;
    EXPECT_FALSE(placeHinges(door(2), c.left, hinge, cfg).ok());
    cfg = hingeConfig();
    cfg.count = 1;
    EXPECT_FALSE(placeHinges(door(2), c.left, hinge, cfg).ok());
    EXPECT_FALSE(placeHinges(door(2), c.left, def(ids::kShelfPin5), hingeConfig()).ok());
    EXPECT_FALSE(placeHinges(door(2), c.left, hinge, HingeConfig{}).ok());
}

// Door x 2..398, z 2..718; inner face A at y = -2. Door local x = z - 2, local y = x - 2.
TEST(PlaceHandle, CentredHorizontal) {
    const auto d = door(2);
    const auto r = placeHandle(d, def(ids::kHandleBar128), handleConfig());
    ASSERT_TRUE(r.ok()) << r.error().message;
    const Placement& p = r.value();
    ASSERT_EQ(p.instances.size(), 1u);
    EXPECT_EQ(p.instances[0].quantity, 1);
    EXPECT_EQ(p.instances[0].parts, (std::vector<domain::PartId>{d.id}));
    // Centre of the door: x = 200, z = 360; on face A (y = -2).
    expectVec(p.instances[0].position, {200, -2, 360});
    ASSERT_EQ(p.operations.size(), 1u);
    const auto& ops = p.operations[0].operations;
    ASSERT_EQ(ops.size(), 2u);
    // Holes at x = 200 -/+ 64 = 136, 264 -> local y = 134, 262; z = 360 -> local x = 358.
    expectHole(ops[0], Hole{Face::A, 358, 134, 5, 0, true});
    expectHole(ops[1], Hole{Face::A, 358, 262, 5, 0, true});
    EXPECT_EQ(ops[0].id.str(), "cab1/hw.handle/op.1");
    EXPECT_EQ(ops[1].id.str(), "cab1/hw.handle/op.2");
    EXPECT_EQ(ops[0].source, "cab1/hw.handle");
}

TEST(PlaceHandle, VerticalWithEdgeOffsets) {
    const auto d = door(2);
    auto cfg = handleConfig();
    cfg.orientation = HandleOrientation::Vertical;
    cfg.horizontalAnchor = HorizontalAnchor::FromRight;
    cfg.horizontalOffset = 40; // x = 398 - 40 = 358 -> local y = 356
    cfg.verticalAnchor = VerticalAnchor::FromTop;
    cfg.verticalOffset = 100; // z = 718 - 100 = 618; holes at 554 and 682 -> local x = 552, 680
    cfg.face = Face::B;
    auto r = placeHandle(d, def(ids::kHandleBar128), cfg);
    ASSERT_TRUE(r.ok()) << r.error().message;
    const auto& ops = r.value().operations[0].operations;
    expectHole(ops[0], Hole{Face::B, 552, 356, 5, 0, true}); // lower hole first
    expectHole(ops[1], Hole{Face::B, 680, 356, 5, 0, true});
    expectVec(r.value().instances[0].position, {358, -20, 618}); // on face B = outer face y = -20

    cfg.horizontalAnchor = HorizontalAnchor::FromLeft; // x = 2 + 40 = 42 -> local y = 40
    cfg.verticalAnchor = VerticalAnchor::FromBottom;   // z = 2 + 100 = 102; holes 38, 166 -> local x 36, 164
    r = placeHandle(d, def(ids::kHandleBar128), cfg);
    ASSERT_TRUE(r.ok()) << r.error().message;
    expectHole(r.value().operations[0].operations[0], Hole{Face::B, 36, 40, 5, 0, true});
    expectHole(r.value().operations[0].operations[1], Hole{Face::B, 164, 40, 5, 0, true});
}

TEST(PlaceHandle, DrawerFrontWithHorizontalLength) {
    // Drawer front 596 x 140 x 18: length -> +X, width -> +Z, thickness -> cross(X, Z) = -Y.
    // Origin (2, -2, 300): x 2..598, y -20..-2, z 300..440. Local x = x - 2, local y = z - 300.
    const auto f = makePart("df", "Drawer front", 596, 140, 18, Transform::fromAxes({1, 0, 0}, {0, 0, 1}, {2, -2, 300}));
    const auto r = placeHandle(f, def(ids::kHandleBar128), handleConfig());
    ASSERT_TRUE(r.ok()) << r.error().message;
    // Centre x = 300, z = 370: holes at x = 236, 364 -> local x = 234, 362; local y = 70.
    expectHole(r.value().operations[0].operations[0], Hole{Face::A, 234, 70, 5, 0, true});
    expectHole(r.value().operations[0].operations[1], Hole{Face::A, 362, 70, 5, 0, true});
    // A vertical 128 mm handle cannot fit a 140 mm high front with 5 mm edge material:
    // holes at z = 370 -/+ 64 -> 6 mm from the edges, minus radius 2.5 = 3.5 mm < 5 mm.
    auto cfg = handleConfig();
    cfg.orientation = HandleOrientation::Vertical;
    const auto bad = placeHandle(f, def(ids::kHandleBar128), cfg);
    ASSERT_FALSE(bad.ok());
    EXPECT_NE(bad.error().message.find("only 3.5 mm"), std::string::npos) << bad.error().message;
    EXPECT_NE(bad.error().message.find("'Drawer front'"), std::string::npos);
    cfg.minEdgeMaterial = 3;
    EXPECT_TRUE(placeHandle(f, def(ids::kHandleBar128), cfg).ok());
}

TEST(PlaceHandle, Failures) {
    Cabinet c;
    const auto d = door(2);
    const auto handle = def(ids::kHandleBar128);
    // Vertical handle centred 60 mm below the top: upper hole at z = 658 + 64 = 722 > 718.
    auto cfg = handleConfig();
    cfg.orientation = HandleOrientation::Vertical;
    cfg.verticalAnchor = VerticalAnchor::FromTop;
    cfg.verticalOffset = 60;
    auto r = placeHandle(d, handle, cfg);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, core::ErrorCode::ConstraintViolation);
    EXPECT_NE(r.error().message.find("off the panel"), std::string::npos) << r.error().message;
    // Horizontal handle centred 5 mm from the left edge.
    cfg = handleConfig();
    cfg.horizontalAnchor = HorizontalAnchor::FromLeft;
    cfg.horizontalOffset = 5;
    EXPECT_FALSE(placeHandle(d, handle, cfg).ok());
    // A horizontal panel is not a front.
    r = placeHandle(c.shelf, handle, handleConfig());
    ASSERT_FALSE(r.ok());
    EXPECT_NE(r.error().message.find("not vertical"), std::string::npos) << r.error().message;
    // Edge face, wrong kind, missing parameter, missing id.
    cfg = handleConfig();
    cfg.face = Face::L1;
    EXPECT_FALSE(placeHandle(d, handle, cfg).ok());
    r = placeHandle(d, def(ids::kConfirmat7x50), handleConfig());
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, core::ErrorCode::Unsupported);
    auto broken = handle;
    broken.params.erase(keys::kHoleSpacing);
    r = placeHandle(d, broken, handleConfig());
    ASSERT_FALSE(r.ok());
    EXPECT_NE(r.error().message.find("hole_spacing"), std::string::npos);
    EXPECT_FALSE(placeHandle(d, handle, HandleConfig{}).ok());
}
