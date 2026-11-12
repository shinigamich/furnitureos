// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/hardware/corner_joint.hpp"
#include "fos/machining/validation.hpp"
#include "test_util.hpp"

using namespace fos;
using namespace fos::hardware;
using namespace fos::hardware::testing;
using domain::Face;
using domain::Hole;
using geometry::Transform;

namespace {
CornerJointConfig config(const char* id) {
    CornerJointConfig c;
    c.instanceId = domain::HardwareInstanceId(id);
    return c;
}
} // namespace

TEST(FastenerPositions, AutomaticCount) {
    // 560 mm joint, 50 mm end offsets: span 460; ceil(460 / 250) = 2 intervals -> 3 fasteners.
    EXPECT_EQ(fastenerPositions(560, 50, 250).value(), (std::vector<double>{50, 280, 510}));
    // 300 mm: span 200 -> ceil(0.8) = 1 interval -> 2 fasteners.
    EXPECT_EQ(fastenerPositions(300, 50, 250).value(), (std::vector<double>{50, 250}));
    // 600 mm: span 500 -> exactly 2 intervals of 250 -> 3; 601 mm: span 501 -> 3 intervals -> 4.
    EXPECT_EQ(fastenerPositions(600, 50, 250).value(), (std::vector<double>{50, 300, 550}));
    EXPECT_EQ(fastenerPositions(601, 50, 250).value().size(), 4u);
    // Exactly 2 * offset: one centred fastener.
    EXPECT_EQ(fastenerPositions(100, 50, 250).value(), (std::vector<double>{50}));
    // Shorter than 2 * offset: error with numbers, no guessing.
    const auto bad = fastenerPositions(80, 50, 250);
    ASSERT_FALSE(bad.ok());
    EXPECT_NE(bad.error().message.find("80 mm"), std::string::npos) << bad.error().message;
    EXPECT_NE(bad.error().message.find("100 mm"), std::string::npos);
    EXPECT_FALSE(fastenerPositions(0, 50, 250).ok());
    EXPECT_FALSE(fastenerPositions(560, 50, 0).ok());
}

TEST(FastenerPositions, ExplicitCount) {
    EXPECT_EQ(fastenerPositions(560, 50, 250, 1).value(), (std::vector<double>{280}));
    EXPECT_EQ(fastenerPositions(80, 50, 250, 1).value(), (std::vector<double>{40}));
    EXPECT_EQ(fastenerPositions(560, 50, 250, 2).value(), (std::vector<double>{50, 510}));
    // 4 fasteners: 50 + k * 460 / 3.
    const auto p = fastenerPositions(560, 50, 250, 4).value();
    ASSERT_EQ(p.size(), 4u);
    EXPECT_NEAR(p[1], 50 + 460.0 / 3, 1e-9);
    EXPECT_NEAR(p[3], 510, 1e-9);
    EXPECT_FALSE(fastenerPositions(100, 50, 250, 2).ok());
    EXPECT_FALSE(fastenerPositions(560, 50, 250, -1).ok());
}

TEST(DetectButtJoint, FixtureCorners) {
    Cabinet c;
    // Bottom spans x 18..782: its W1 edge (local x = 0 -> x = 18) lies on the left side's inner face B (x = 18).
    auto j = detectButtJoint(c.left, c.bottom);
    ASSERT_TRUE(j.ok()) << j.error().message;
    EXPECT_EQ(j.value().faceOnFacePart, Face::B);
    EXPECT_EQ(j.value().edgeOnEdgePart, Face::W1);
    EXPECT_NEAR(j.value().along.min, 0, 1e-9);
    EXPECT_NEAR(j.value().along.max, 560, 1e-9); // full depth of the bottom
    // Top's W2 edge (local x = 764 -> x = 782) lies on the right side's inner face A (x = 782).
    j = detectButtJoint(c.right, c.top);
    ASSERT_TRUE(j.ok()) << j.error().message;
    EXPECT_EQ(j.value().faceOnFacePart, Face::A);
    EXPECT_EQ(j.value().edgeOnEdgePart, Face::W2);
    // The shelf (y 20..560) also touches the left side; its edge is 540 long.
    j = detectButtJoint(c.left, c.shelf);
    ASSERT_TRUE(j.ok());
    EXPECT_EQ(j.value().edgeOnEdgePart, Face::W1);
    EXPECT_NEAR(j.value().along.length(), 540, 1e-9);
}

TEST(DetectButtJoint, Failures) {
    Cabinet c;
    // The two sides are parallel and 764 mm apart: no edge of one touches a face of the other.
    auto j = detectButtJoint(c.left, c.right);
    ASSERT_FALSE(j.ok());
    EXPECT_EQ(j.error().code, core::ErrorCode::ConstraintViolation);
    EXPECT_NE(j.error().message.find("'Right side'"), std::string::npos) << j.error().message;
    EXPECT_NE(j.error().message.find("'Left side'"), std::string::npos);
    // Roles swapped: no edge of the left side lies on a face of the bottom (the side's W1
    // edge is in the plane z = 0 like the bottom's face B, but both look downwards).
    EXPECT_FALSE(detectButtJoint(c.bottom, c.left).ok());
    // Bottom pulled 5 mm away from the left side (x 23..787).
    domain::Part moved = c.bottom;
    moved.placement = Transform::translation({23, 0, 0});
    j = detectButtJoint(c.left, moved);
    ASSERT_FALSE(j.ok());
    EXPECT_NE(j.error().message.find("5 mm away"), std::string::npos) << j.error().message;
    // Within the tolerance it is accepted (0.05 mm gap, default tolerance 0.1).
    moved.placement = Transform::translation({18.05, 0, 0});
    EXPECT_TRUE(detectButtJoint(c.left, moved).ok());
    EXPECT_FALSE(detectButtJoint(c.left, moved, 0.01).ok());
    // Bottom lowered by 9 mm (z -9..9): only 9 of its 18 mm thickness rest on the side (z 0..720).
    moved.placement = Transform::translation({18, 0, -9});
    j = detectButtJoint(c.left, moved);
    ASSERT_FALSE(j.ok());
    EXPECT_NE(j.error().message.find("only partly"), std::string::npos) << j.error().message;
    EXPECT_NE(j.error().message.find("9 mm of its 18 mm"), std::string::npos) << j.error().message;
    // Bottom shifted completely in front of the side (y -600..-40): same plane, no overlap.
    moved.placement = Transform::translation({18, -600, 0});
    j = detectButtJoint(c.left, moved);
    ASSERT_FALSE(j.ok());
    EXPECT_NE(j.error().message.find("do not overlap"), std::string::npos) << j.error().message;
    // Degenerate part.
    moved = c.bottom;
    moved.thickness = 0;
    EXPECT_EQ(detectButtJoint(c.left, moved).error().code, core::ErrorCode::InvalidArgument);
}

TEST(ConnectCorner, ConfirmatsLeftSideAndBottom) {
    Cabinet c;
    const auto r = connectCorner(c.left, c.bottom, def(ids::kConfirmat7x50), config("cab1/hw.corner.bl"));
    ASSERT_TRUE(r.ok()) << r.error().message;
    const Placement& p = r.value();

    ASSERT_EQ(p.instances.size(), 1u);
    const auto& inst = p.instances[0];
    EXPECT_EQ(inst.id.str(), "cab1/hw.corner.bl");
    EXPECT_EQ(inst.definition.str(), "hw.confirmat_7x50");
    EXPECT_EQ(inst.quantity, 3); // 560 mm joint -> 50 / 280 / 510
    EXPECT_EQ(inst.parts, (std::vector<domain::PartId>{c.left.id, c.bottom.id}));
    // First fastener on the contact plane: x = 18, y = 50, z = centre of the bottom = 9.
    expectVec(inst.position, {18, 50, 9});

    ASSERT_EQ(p.operations.size(), 2u);
    EXPECT_EQ(p.operations[0].part, c.left.id);
    EXPECT_EQ(p.operations[1].part, c.bottom.id);
    const auto& sideOps = p.operations[0].operations;
    const auto& bottomOps = p.operations[1].operations;
    ASSERT_EQ(sideOps.size(), 3u);
    ASSERT_EQ(bottomOps.size(), 3u);

    // Side: local x = assembly z = 9 (centre of the 18 mm bottom), local y = assembly y.
    // The contact face is B (inner); the screw enters from the opposite, outer face A.
    // Bottom: edge W1, u = local y = assembly y, v = 9 = half of 18.
    // Pilot depth = 50 - 18 + 5 = 37.
    const double ys[] = {50, 280, 510};
    for (int i = 0; i < 3; ++i) {
        expectHole(sideOps[i], Hole{Face::A, 9, ys[i], 7, 0, true});
        expectHole(bottomOps[i], Hole{Face::W1, ys[i], 9, 5, 37, false});
        EXPECT_EQ(sideOps[i].id.str(), "cab1/hw.corner.bl/op." + std::to_string(2 * i + 1));
        EXPECT_EQ(bottomOps[i].id.str(), "cab1/hw.corner.bl/op." + std::to_string(2 * i + 2));
        EXPECT_EQ(sideOps[i].source, "cab1/hw.corner.bl");
        EXPECT_EQ(bottomOps[i].source, "cab1/hw.corner.bl");

        // Coaxiality proven in the assembly frame.
        const auto a = machining::holeAxisInAssembly(c.left, hole(sideOps[i]));
        const auto b = machining::holeAxisInAssembly(c.bottom, hole(bottomOps[i]));
        expectVec(a.start, {0, ys[i], 9});      // outer face of the side
        expectVec(a.direction, {1, 0, 0});      // screw travels in +X
        EXPECT_DOUBLE_EQ(a.length, 18);         // through the side
        expectVec(b.start, {18, ys[i], 9});     // edge of the bottom
        expectVec(b.direction, {1, 0, 0});
        EXPECT_TRUE(coaxial(c.left, hole(sideOps[i]), c.bottom, hole(bottomOps[i])));
        // Edge hole centred in the bottom's thickness.
        EXPECT_DOUBLE_EQ(hole(bottomOps[i]).v, c.bottom.thickness / 2);
    }
    // Pure function: the fixture parts were not modified.
    EXPECT_EQ(c.left.operations.size(), 2u);
    EXPECT_TRUE(c.bottom.operations.empty());
}

TEST(ConnectCorner, ConfirmatsRightSideAndTop) {
    Cabinet c;
    const auto r = connectCorner(c.right, c.top, def(ids::kConfirmat7x50), config("cab1/hw.corner.tr"));
    ASSERT_TRUE(r.ok()) << r.error().message;
    const auto& sideOps = *r.value().operationsFor(c.right.id);
    const auto& topOps = *r.value().operationsFor(c.top.id);
    ASSERT_EQ(sideOps.size(), 3u);
    // Top spans z 702..720 -> centre z = 711 = local x of the side. Contact face of the right
    // side is A (inner, x = 782) -> through hole from B (outer, x = 800). Top edge W2.
    const double ys[] = {50, 280, 510};
    for (int i = 0; i < 3; ++i) {
        expectHole(sideOps[i], Hole{Face::B, 711, ys[i], 7, 0, true});
        expectHole(topOps[i], Hole{Face::W2, ys[i], 9, 5, 37, false});
        const auto a = machining::holeAxisInAssembly(c.right, hole(sideOps[i]));
        const auto b = machining::holeAxisInAssembly(c.top, hole(topOps[i]));
        expectVec(a.start, {800, ys[i], 711});
        expectVec(a.direction, {-1, 0, 0});
        expectVec(b.start, {782, ys[i], 711});
        expectVec(b.direction, {-1, 0, 0});
        EXPECT_TRUE(coaxial(c.right, hole(sideOps[i]), c.top, hole(topOps[i])));
    }
    expectVec(r.value().instances[0].position, {782, 50, 711});
}

TEST(ConnectCorner, DowelsRightSideAndTop) {
    Cabinet c;
    auto cfg = config("cab1/hw.dowels.tr");
    const auto r = connectCorner(c.right, c.top, def(ids::kDowel8x30), cfg);
    ASSERT_TRUE(r.ok()) << r.error().message;
    const auto& sideOps = *r.value().operationsFor(c.right.id);
    const auto& topOps = *r.value().operationsFor(c.top.id);
    ASSERT_EQ(sideOps.size(), 3u);
    const double ys[] = {50, 280, 510};
    for (int i = 0; i < 3; ++i) {
        // Blind 8 x 12 hole in the CONTACT face A of the side; 8 x 20 in the top's edge.
        expectHole(sideOps[i], Hole{Face::A, 711, ys[i], 8, 12, false});
        expectHole(topOps[i], Hole{Face::W2, ys[i], 9, 8, 20, false});
        const auto a = machining::holeAxisInAssembly(c.right, hole(sideOps[i]));
        const auto b = machining::holeAxisInAssembly(c.top, hole(topOps[i]));
        // Both start on the contact plane x = 782 and run in opposite directions.
        expectVec(a.start, {782, ys[i], 711});
        expectVec(b.start, {782, ys[i], 711});
        expectVec(a.direction, {1, 0, 0});
        expectVec(b.direction, {-1, 0, 0});
        EXPECT_TRUE(coaxial(c.right, hole(sideOps[i]), c.top, hole(topOps[i])));
    }
    // Configurable split and explicit count: 10 / 22, two dowels at 50 and 510.
    cfg.dowelFaceDepth = 10;
    cfg.dowelEdgeDepth = 22;
    cfg.count = 2;
    const auto r2 = connectCorner(c.left, c.bottom, def(ids::kDowel8x30), cfg);
    ASSERT_TRUE(r2.ok()) << r2.error().message;
    const auto& s2 = *r2.value().operationsFor(c.left.id);
    const auto& b2 = *r2.value().operationsFor(c.bottom.id);
    ASSERT_EQ(s2.size(), 2u);
    expectHole(s2[0], Hole{Face::B, 9, 50, 8, 10, false});
    expectHole(s2[1], Hole{Face::B, 9, 510, 8, 10, false});
    expectHole(b2[1], Hole{Face::W1, 510, 9, 8, 22, false});
    EXPECT_EQ(r2.value().instances[0].quantity, 2);
    // A split that leaves 1 mm of the 18 mm side is refused (must keep more than 2 mm).
    cfg.dowelFaceDepth = 17;
    const auto r3 = connectCorner(c.left, c.bottom, def(ids::kDowel8x30), cfg);
    ASSERT_FALSE(r3.ok());
    EXPECT_NE(r3.error().message.find("17 mm deep"), std::string::npos) << r3.error().message;
    EXPECT_NE(r3.error().message.find("'Left side'"), std::string::npos);
}

TEST(ConnectCorner, UprightStandingOnABase) {
    // Base 600 x 400 x 18 lying flat at the origin: top face A at z = 18.
    const auto base = makePart("base", "Base", 600, 400, 18, Transform::identity());
    // Upright 500 x 400 x 18: length -> +Z, width -> +Y, thickness -> -X, origin (118, 0, 18):
    // occupies x 100..118, y 0..400, z 18..518. Its W1 edge (local x = 0) stands on the base.
    const auto upright = makePart("up", "Upright", 500, 400, 18, Transform::fromAxes({0, 0, 1}, {0, 1, 0}, {118, 0, 18}));
    const auto r = connectCorner(base, upright, def(ids::kConfirmat7x50), config("j1"));
    ASSERT_TRUE(r.ok()) << r.error().message;
    const auto& baseOps = *r.value().operationsFor(base.id);
    const auto& upOps = *r.value().operationsFor(upright.id);
    // Joint 400 mm: span 300 -> ceil(1.2) = 2 intervals -> fasteners at 50, 200, 350.
    // Upright edge centre line: local z = 9 -> x = 118 - 9 = 109. Screws enter from below (face B).
    ASSERT_EQ(baseOps.size(), 3u);
    const double ys[] = {50, 200, 350};
    for (int i = 0; i < 3; ++i) {
        expectHole(baseOps[i], Hole{Face::B, 109, ys[i], 7, 0, true});
        expectHole(upOps[i], Hole{Face::W1, ys[i], 9, 5, 37, false});
        expectVec(machining::holeAxisInAssembly(upright, hole(upOps[i])).start, {109, ys[i], 18});
        expectVec(machining::holeAxisInAssembly(upright, hole(upOps[i])).direction, {0, 0, 1});
        EXPECT_TRUE(coaxial(base, hole(baseOps[i]), upright, hole(upOps[i])));
    }
}

TEST(ConnectCorner, Failures) {
    Cabinet c;
    const auto confirmat = def(ids::kConfirmat7x50);
    // Parts that do not touch.
    auto r = connectCorner(c.left, c.right, confirmat, config("x"));
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, core::ErrorCode::ConstraintViolation);
    EXPECT_NE(r.error().message.find("No butt joint"), std::string::npos) << r.error().message;
    // Missing instance id.
    r = connectCorner(c.left, c.bottom, confirmat, CornerJointConfig{});
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, core::ErrorCode::InvalidArgument);
    // Wrong kind of fitting.
    r = connectCorner(c.left, c.bottom, def(ids::kHingeCup35), config("x"));
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, core::ErrorCode::Unsupported);
    // Confirmat 7x50 through a 60 mm thick face panel: too short.
    auto thick = makePart("thick", "Thick side", 720, 560, 60, Transform::fromAxes({0, 0, 1}, {0, 1, 0}, {18, 0, 0}));
    r = connectCorner(thick, c.bottom, confirmat, config("x")); // thick occupies x -42..18, inner face B at x = 18
    ASSERT_FALSE(r.ok());
    EXPECT_NE(r.error().message.find("50 mm long"), std::string::npos) << r.error().message;
    EXPECT_NE(r.error().message.find("60 mm thick"), std::string::npos);
    // Fasteners at the very ends of the joint (offset 0): the 7 mm hole at y = 0 would be cut by the side's edge.
    auto cfg = config("x");
    cfg.minEndOffset = 0;
    cfg.count = 2;
    r = connectCorner(c.left, c.bottom, confirmat, cfg);
    ASSERT_FALSE(r.ok());
    EXPECT_NE(r.error().message.find("(9, 0)"), std::string::npos) << r.error().message;
    // Joint too short for the end offsets: a 90 mm wide rail under the top.
    auto rail = makePart("rail", "Rail", 764, 90, 18, Transform::translation({18, 0, 600}));
    r = connectCorner(c.left, rail, confirmat, config("x"));
    ASSERT_FALSE(r.ok());
    EXPECT_NE(r.error().message.find("90 mm long"), std::string::npos) << r.error().message;
    EXPECT_NE(r.error().message.find("'Rail'"), std::string::npos);
    cfg = config("x");
    cfg.count = 1; // an explicit single fastener is fine: centre of the rail, y = 45, z = 609
    r = connectCorner(c.left, rail, confirmat, cfg);
    ASSERT_TRUE(r.ok()) << r.error().message;
    expectHole(r.value().operations[0].operations[0], Hole{Face::A, 609, 45, 7, 0, true});
    // Definition lacking a parameter.
    auto broken = confirmat;
    broken.params.erase(keys::kPilotDiameter);
    r = connectCorner(c.left, c.bottom, broken, config("x"));
    ASSERT_FALSE(r.ok());
    EXPECT_NE(r.error().message.find("pilot_diameter"), std::string::npos) << r.error().message;
}

TEST(ConnectCorner, WholeCarcassValidatesCleanAndReappliesIdempotently) {
    Cabinet c;
    const auto confirmat = def(ids::kConfirmat7x50);
    struct J { const domain::Part* face; const domain::Part* edge; const char* id; };
    const J joints[] = {{&c.left, &c.bottom, "cab1/hw.c.bl"}, {&c.right, &c.bottom, "cab1/hw.c.br"},
                        {&c.left, &c.top, "cab1/hw.c.tl"}, {&c.right, &c.top, "cab1/hw.c.tr"}};
    for (int pass = 0; pass < 2; ++pass) // second pass: recalculation replaces, never duplicates
        for (const auto& j : joints) {
            const auto r = connectCorner(*j.face, *j.edge, confirmat, config(j.id));
            ASSERT_TRUE(r.ok()) << r.error().message;
            ASSERT_TRUE(applyPlacement(c.a, r.value()).ok());
        }
    // Left side: 2 fixture pin holes + 3 (bottom) + 3 (top). Bottom/top: 3 per end.
    EXPECT_EQ(c.left.operations.size(), 8u);
    EXPECT_EQ(c.right.operations.size(), 6u);
    EXPECT_EQ(c.bottom.operations.size(), 6u);
    EXPECT_EQ(c.top.operations.size(), 6u);
    EXPECT_EQ(c.a.hardware.size(), 5u); // fixture shelf pins + 4 corner joints
    for (const auto& part : c.a.parts) {
        const auto ds = machining::validateOperations(part);
        EXPECT_TRUE(ds.empty()) << part.name << ": " << (ds.empty() ? "" : ds[0].message);
    }
    // Unknown part: error, assembly untouched.
    Placement bogus;
    bogus.operations.push_back({domain::PartId("nope"), {}});
    const auto bad = applyPlacement(c.a, bogus);
    ASSERT_FALSE(bad.ok());
    EXPECT_EQ(bad.error().code, core::ErrorCode::NotFound);
    EXPECT_NE(bad.error().message.find("'nope'"), std::string::npos);
    EXPECT_EQ(c.a.hardware.size(), 5u);
}
