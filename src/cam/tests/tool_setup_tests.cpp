// SPDX-License-Identifier: AGPL-3.0-only
#include "cam_test_fixtures.hpp"

#include "fos/cam/setup.hpp"
#include "fos/machining/face.hpp"

#include <gtest/gtest.h>

using namespace fos;
using namespace fos::cam;
using domain::Face;

TEST(CamTools, MissingDrillNamesAvailableDiameters) {
    const auto lib = test::testTools();
    auto r = lib.selectDrill(7, 10);
    ASSERT_FALSE(r);
    EXPECT_EQ(r.error().message, "No drill of diameter 7 mm in the tool library (available: 5, 8, 35).");
    EXPECT_EQ(r.error().code, core::ErrorCode::NotFound);
}

TEST(CamTools, DrillWithTooShortFluteIsRejected) {
    const auto lib = test::testTools();
    auto r = lib.selectDrill(35, 20); // drill.35 has 15 mm flute
    ASSERT_FALSE(r);
    EXPECT_EQ(r.error().message,
              "Drill 'drill.35' (T3, 35 mm) has flute length 15 mm but 20 mm of cutting depth is needed.");
    auto ok = lib.selectDrill(5, 13);
    ASSERT_TRUE(ok);
    EXPECT_EQ(ok.value()->number, 1);
}

TEST(CamTools, GrooveCutterSelection) {
    auto lib = test::testTools();
    ASSERT_TRUE(lib.add(test::endMill("em.4", 5, 4, 22, 2)));
    auto r = lib.selectGrooveCutter(8, 8); // largest <= 8: em.6
    ASSERT_TRUE(r);
    EXPECT_EQ(r.value()->id, "em.6");
    auto r2 = lib.selectGrooveCutter(5, 8); // only em.4 fits
    ASSERT_TRUE(r2);
    EXPECT_EQ(r2.value()->id, "em.4");
    auto r3 = lib.selectGrooveCutter(3, 8);
    ASSERT_FALSE(r3);
    EXPECT_NE(r3.error().message.find("No centre-cutting end mill with diameter <= 3 mm"), std::string::npos);
}

TEST(CamTools, AddRejectsDuplicatesAndBadValues) {
    auto lib = test::testTools();
    auto dupNumber = lib.add(test::drill("other", 1, 6, 30));
    ASSERT_FALSE(dupNumber);
    EXPECT_NE(dupNumber.error().message.find("already taken by 'drill.5'"), std::string::npos);
    EXPECT_FALSE(lib.add(test::drill("drill.5", 9, 6, 30)));
    EXPECT_FALSE(lib.add(test::drill("zero", 9, 0, 30)));
    EXPECT_FALSE(lib.add(test::endMill("nodoc", 9, 6, 20, 0)));
    ASSERT_EQ(lib.tools().size(), 4u);
}

TEST(CamProfile, ValidationAlwaysWarnsUnverified) {
    auto p = test::testProfile();
    auto d = validateProfile(p);
    ASSERT_EQ(d.size(), 1u);
    EXPECT_EQ(d[0].code, "cam.profile.unverified");
    EXPECT_FALSE(p.verified);
    p.safeZ = 500;
    p.maxFeed = 0;
    d = validateProfile(p);
    EXPECT_TRUE(core::hasErrors(d));
}

// Left side of the fixture cabinet: 720 x 560 x 18, holes on face B at (346, 37) and (346, 523).
TEST(CamSetup, FaceAUpPutsFaceBHolesIntoOtherSetup) {
    const auto part = test::leftSide();
    auto plan = planSetup(part, {Face::A, {10, 20, 0}});
    ASSERT_TRUE(plan);
    EXPECT_TRUE(plan.value().machinableNow.empty());
    ASSERT_EQ(plan.value().needsOtherSetup.size(), 2u);
    EXPECT_EQ(plan.value().needsOtherSetup[0].requiredFaceUp, Face::B);
    EXPECT_EQ(plan.value().needsOtherSetup[0].id.str(), "cab1/side.left/op.pin.front");
}

TEST(CamSetup, FaceAUpMappingIsTranslation) {
    const auto part = test::leftSide();
    auto f = makeSetupFrame(part, {Face::A, {10, 20, 0}});
    ASSERT_TRUE(f);
    // A point (u, v) = (346, 37) on face A -> machine (10 + 346, 20 + 37) = (356, 57), top Z = 18.
    const auto xy = faceToMachineXY(part, f.value(), {346, 37});
    EXPECT_DOUBLE_EQ(xy.x, 356);
    EXPECT_DOUBLE_EQ(xy.y, 57);
    const auto p = f.value().partToMachine(machining::faceToPart(part, Face::A, 346, 37));
    EXPECT_DOUBLE_EQ(p.x, 356);
    EXPECT_DOUBLE_EQ(p.y, 57);
    EXPECT_DOUBLE_EQ(p.z, 18);
}

TEST(CamSetup, FaceBUpMapsHolesToThePhysicalSpot) {
    const auto part = test::leftSide();
    const cam::Setup setup{Face::B, {10, 20, 0}};
    auto fr = makeSetupFrame(part, setup);
    ASSERT_TRUE(fr);
    const auto& f = fr.value();
    auto plan = planSetup(part, setup);
    ASSERT_TRUE(plan);
    ASSERT_EQ(plan.value().machinableNow.size(), 2u);
    // Independent ground truth: the panel is rotated 180 deg about its length axis (local X):
    // R = diag(1, -1, -1); then translated so the stock occupies [10,730] x [20,580] x [0,18]:
    // machine = (10, 20, 0) + (0, 560, 18) + R * p.
    auto expected = [](geometry::Vec3 p) { return geometry::Vec3{10 + p.x, 20 + 560 - p.y, 0 + 18 - p.z}; };
    for (const auto& planned : plan.value().machinableNow) {
        const auto& h = std::get<domain::Hole>(part.operations[planned.index].shape);
        const auto partPoint = machining::faceToPart(part, Face::B, h.u, h.v); // (u, v, 0)
        const auto e = expected(partPoint);
        const auto m = f.partToMachine(partPoint);
        EXPECT_NEAR(m.x, e.x, 1e-9);
        EXPECT_NEAR(m.y, e.y, 1e-9);
        EXPECT_NEAR(m.z, 18, 1e-9); // face B is on top
        const auto xy = faceToMachineXY(part, f, {h.u, h.v}); // via machining flip helper
        EXPECT_NEAR(xy.x, e.x, 1e-9);
        EXPECT_NEAR(xy.y, e.y, 1e-9);
    }
    // Hand values: front pin (346, 37) -> (356, 20 + 560 - 37) = (356, 543); back pin (346, 523) -> (356, 57).
    const auto front = holeTarget(part, f, part.operations[0]);
    EXPECT_DOUBLE_EQ(front.centre.x, 356);
    EXPECT_DOUBLE_EQ(front.centre.y, 543);
    EXPECT_DOUBLE_EQ(front.floorZ, 5); // 18 - 13
    // The front edge L1 (part y = 0) is now at machine Y = 580, so the front pin is 37 mm from it.
    EXPECT_DOUBLE_EQ(f.partToMachine({0, 0, 0}).y, 580);
    EXPECT_DOUBLE_EQ(580 - front.centre.y, 37);
}

TEST(CamSetup, EdgeOperationsAndPocketsAreUnsupportedNotSkipped) {
    auto part = test::panel();
    part.operations.push_back(test::op("edge", domain::Hole{Face::L1, 100, 9, 8, 30, false}));
    part.operations.push_back(test::op("top", domain::Hole{Face::A, 100, 100, 5, 10, false}));
    domain::Pocket pk;
    pk.face = Face::A;
    pk.contour = {{200, 200}, {250, 200}, {250, 250}, {200, 250}};
    pk.depth = 5;
    part.operations.push_back(test::op("pocket", pk));
    part.operations.push_back(test::op("under", domain::Hole{Face::B, 100, 100, 5, 10, false}));
    auto plan = planSetup(part, {Face::A, {}});
    ASSERT_TRUE(plan);
    const auto& p = plan.value();
    ASSERT_EQ(p.machinableNow.size(), 1u);
    EXPECT_EQ(p.machinableNow[0].id.str(), "top");
    ASSERT_EQ(p.unsupported.size(), 2u);
    EXPECT_EQ(p.unsupported[0].id.str(), "edge");
    EXPECT_NE(p.unsupported[0].reason.find("edge face"), std::string::npos);
    EXPECT_EQ(p.unsupported[1].id.str(), "pocket");
    EXPECT_NE(p.unsupported[1].reason.find("pocket milling is not implemented"), std::string::npos);
    ASSERT_EQ(p.needsOtherSetup.size(), 1u);
    EXPECT_EQ(p.needsOtherSetup[0].id.str(), "under");
}

TEST(CamSetup, EdgeFaceUpIsRejected) {
    auto r = makeSetupFrame(test::panel(), {Face::L1, {}});
    ASSERT_FALSE(r);
    EXPECT_EQ(r.error().code, core::ErrorCode::Unsupported);
}
