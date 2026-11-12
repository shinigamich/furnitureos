// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/hardware/shelf_supports.hpp"
#include "fos/machining/validation.hpp"
#include "test_util.hpp"

using namespace fos;
using namespace fos::hardware;
using namespace fos::hardware::testing;
using domain::Face;
using domain::Hole;
using geometry::Transform;

namespace {
ShelfSupportConfig config(const char* id = "cab1/hw.pins.1") {
    ShelfSupportConfig c;
    c.instanceId = domain::HardwareInstanceId(id);
    return c;
}
} // namespace

// Hand computation from the fixture geometry (NOT from the fixture's illustrative holes):
//   shelf.1 is placed at (18, 20, 351), 764 x 540 x 18 -> x 18..782, y 20..560, z 351..369.
//   Underside z = 351; pin diameter 5 -> pin centre z = 351 - 2.5 = 348.5 (top of the pin hole
//   touches the underside at z = 351).
//   Default setbacks 50 mm from the shelf's front (y = 20) and back (y = 560) edges:
//   y = 70 and y = 510.
//   Left side: local x = assembly z, local y = assembly y; inner face is B (x = 18).
//     -> holes on face B at (348.5, 70) and (348.5, 510).
//   Right side: same local axes; inner face is A (x = 782) -> face A at (348.5, 70), (348.5, 510).
TEST(ShelfSupports, FixtureShelf) {
    Cabinet c;
    const auto r = placeShelfSupports(c.shelf, c.left, c.right, def(ids::kShelfPin5), config());
    ASSERT_TRUE(r.ok()) << r.error().message;
    const Placement& p = r.value();
    ASSERT_EQ(p.instances.size(), 1u);
    EXPECT_EQ(p.instances[0].quantity, 4);
    EXPECT_EQ(p.instances[0].definition.str(), "hw.shelf_pin_5");
    EXPECT_EQ(p.instances[0].parts, (std::vector<domain::PartId>{c.left.id, c.right.id, c.shelf.id}));
    expectVec(p.instances[0].position, {18, 70, 348.5});

    ASSERT_EQ(p.operations.size(), 2u);
    EXPECT_EQ(p.operations[0].part, c.left.id);
    EXPECT_EQ(p.operations[1].part, c.right.id);
    const auto& l = p.operations[0].operations;
    const auto& rr = p.operations[1].operations;
    ASSERT_EQ(l.size(), 2u);
    ASSERT_EQ(rr.size(), 2u);
    expectHole(l[0], Hole{Face::B, 348.5, 70, 5, 13, false});
    expectHole(l[1], Hole{Face::B, 348.5, 510, 5, 13, false});
    expectHole(rr[0], Hole{Face::A, 348.5, 70, 5, 13, false});
    expectHole(rr[1], Hole{Face::A, 348.5, 510, 5, 13, false});
    EXPECT_EQ(l[0].id.str(), "cab1/hw.pins.1/op.1");
    EXPECT_EQ(l[1].id.str(), "cab1/hw.pins.1/op.2");
    EXPECT_EQ(rr[0].id.str(), "cab1/hw.pins.1/op.3");
    EXPECT_EQ(rr[1].id.str(), "cab1/hw.pins.1/op.4");
    for (const auto& op : l) EXPECT_EQ(op.source, "cab1/hw.pins.1");

    // In the assembly frame: holes start on the inner faces and are drilled outwards.
    auto a = machining::holeAxisInAssembly(c.left, hole(l[0]));
    expectVec(a.start, {18, 70, 348.5});
    expectVec(a.direction, {-1, 0, 0});
    a = machining::holeAxisInAssembly(c.right, hole(rr[1]));
    expectVec(a.start, {782, 510, 348.5});
    expectVec(a.direction, {1, 0, 0});
    // Top of every pin hole touches the shelf underside: centre z + radius = 351.
    EXPECT_DOUBLE_EQ(a.start.z + a.radius, 351.0);
}

TEST(ShelfSupports, SetbacksSideOrderAndClearance) {
    Cabinet c;
    auto cfg = config("p");
    cfg.setbackFront = 37; // y = 20 + 37 = 57
    cfg.setbackBack = 17;  // y = 560 - 17 = 543
    // Sides given right first: the right side gets op.1 / op.2.
    auto r = placeShelfSupports(c.shelf, c.right, c.left, def(ids::kShelfPin5), cfg);
    ASSERT_TRUE(r.ok()) << r.error().message;
    EXPECT_EQ(r.value().operations[0].part, c.right.id);
    expectHole(r.value().operations[0].operations[0], Hole{Face::A, 348.5, 57, 5, 13, false});
    expectHole(r.value().operations[0].operations[1], Hole{Face::A, 348.5, 543, 5, 13, false});
    expectHole(r.value().operations[1].operations[0], Hole{Face::B, 348.5, 57, 5, 13, false});
    EXPECT_EQ(r.value().operations[1].operations[1].id.str(), "p/op.4");

    // A loose shelf, 762 mm long with 1 mm clearance on each side (x 19..781), lower in the
    // cabinet (underside z = 200 -> pin centre 197.5). Holes stay ON the side faces.
    const auto loose = makePart("s2", "Loose shelf", 762, 540, 18, Transform::translation({19, 20, 200}));
    r = placeShelfSupports(loose, c.left, c.right, def(ids::kShelfPin5), config("p2"));
    ASSERT_TRUE(r.ok()) << r.error().message;
    expectHole(r.value().operations[0].operations[0], Hole{Face::B, 197.5, 70, 5, 13, false});
    expectVec(machining::holeAxisInAssembly(c.left, hole(r.value().operations[0].operations[0])).start, {18, 70, 197.5});
    expectHole(r.value().operations[1].operations[1], Hole{Face::A, 197.5, 510, 5, 13, false});

    // A shelf turned 180 degrees about Z (its local X points to -X, local Y to -Y): the same
    // physical position must give the same holes. Origin (782, 560, 351) -> x 18..782, y 20..560.
    const auto turned = makePart("s3", "Turned shelf", 764, 540, 18,
                                 Transform::fromAxes({-1, 0, 0}, {0, -1, 0}, {782, 560, 351}));
    r = placeShelfSupports(turned, c.left, c.right, def(ids::kShelfPin5), cfg);
    ASSERT_TRUE(r.ok()) << r.error().message;
    expectHole(r.value().operations[0].operations[0], Hole{Face::B, 348.5, 57, 5, 13, false});  // front
    expectHole(r.value().operations[0].operations[1], Hole{Face::B, 348.5, 543, 5, 13, false}); // back
}

TEST(ShelfSupports, AppliedHolesValidateClean) {
    Cabinet c;
    const auto r = placeShelfSupports(c.shelf, c.left, c.right, def(ids::kShelfPin5), config());
    ASSERT_TRUE(r.ok());
    ASSERT_TRUE(applyPlacement(c.a, r.value()).ok());
    EXPECT_EQ(c.left.operations.size(), 4u); // 2 illustrative fixture holes + 2 new
    EXPECT_EQ(c.right.operations.size(), 2u);
    EXPECT_TRUE(machining::validateOperations(c.left).empty());
    EXPECT_TRUE(machining::validateOperations(c.right).empty());
}

TEST(ShelfSupports, Failures) {
    Cabinet c;
    const auto pin = def(ids::kShelfPin5);
    // The top is horizontal: none of its faces looks at a shelf edge.
    auto r = placeShelfSupports(c.shelf, c.left, c.top, pin, config());
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, core::ErrorCode::ConstraintViolation);
    EXPECT_NE(r.error().message.find("'Top'"), std::string::npos) << r.error().message;
    // The same side twice.
    r = placeShelfSupports(c.shelf, c.left, c.left, pin, config());
    ASSERT_FALSE(r.ok());
    EXPECT_NE(r.error().message.find("between the two sides"), std::string::npos) << r.error().message;
    // A shelf that is too short (700 mm: 64 mm short of the right side).
    auto shortShelf = c.shelf;
    shortShelf.length = 700;
    r = placeShelfSupports(shortShelf, c.left, c.right, pin, config());
    ASSERT_FALSE(r.ok());
    EXPECT_NE(r.error().message.find("'Right side'"), std::string::npos) << r.error().message;
    // A vertical "shelf".
    auto tilted = c.shelf;
    tilted.placement = Transform::fromAxes({0, 0, 1}, {0, 1, 0}, {400, 20, 100});
    r = placeShelfSupports(tilted, c.left, c.right, pin, config());
    ASSERT_FALSE(r.ok());
    EXPECT_NE(r.error().message.find("not horizontal"), std::string::npos) << r.error().message;
    // Setbacks larger than the shelf depth (300 + 300 > 540).
    auto cfg = config();
    cfg.setbackFront = cfg.setbackBack = 300;
    r = placeShelfSupports(c.shelf, c.left, c.right, pin, cfg);
    ASSERT_FALSE(r.ok());
    EXPECT_NE(r.error().message.find("540 mm"), std::string::npos) << r.error().message;
    // Shelf lying 1 mm above the floor: pin centre z = -1.5 would be off the side panel.
    auto low = c.shelf;
    low.placement = Transform::translation({18, 20, 1});
    r = placeShelfSupports(low, c.left, c.right, pin, config());
    ASSERT_FALSE(r.ok());
    EXPECT_NE(r.error().message.find("(-1.5, 70)"), std::string::npos) << r.error().message;
    // A 16.5 mm deep hole would leave 1.5 mm of the 18 mm side.
    auto deep = pin;
    deep.params[keys::kHoleDepth] = 16.5;
    r = placeShelfSupports(c.shelf, c.left, c.right, deep, config());
    ASSERT_FALSE(r.ok());
    EXPECT_NE(r.error().message.find("16.5 mm deep"), std::string::npos) << r.error().message;
    // Wrong kind / missing id.
    EXPECT_FALSE(placeShelfSupports(c.shelf, c.left, c.right, def(ids::kDowel8x30), config()).ok());
    EXPECT_FALSE(placeShelfSupports(c.shelf, c.left, c.right, pin, ShelfSupportConfig{}).ok());
}
