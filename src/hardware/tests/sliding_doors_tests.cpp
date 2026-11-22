// SPDX-License-Identifier: AGPL-3.0-only
// Generic sliding door system: door plan, track screw holes, roller holes, errors.
#include "fos/hardware/sliding_doors.hpp"
#include "test_util.hpp"

using namespace fos;
using namespace fos::hardware;
using namespace fos::hardware::testing;
using domain::Face;
using domain::Hole;
using geometry::Transform;

namespace {
const geometry::Vec3 X{1, 0, 0}, Y{0, 1, 0}, Z{0, 0, 1};
/// Wardrobe 1800 x 2400 x 600, 18 mm sides: opening x 18..1782 (1764), z 18..2382 (2364), depth 600.
SlidingOpening wardrobe(double depth = 600) { return {18, 1782, 18, 2382, depth}; }
geometry::Vec3 holePoint(const domain::Part& p, const domain::Operation& op) {
    const auto& h = std::get<Hole>(op.shape);
    return p.placement.applyPoint(machining::faceToPart(p, h.face, h.u, h.v));
}
} // namespace

TEST(SlidingDoors, TwoDoorPlan) {
    auto plan = planSlidingDoors(def(ids::kSlidingGeneric), wardrobe(), 2, 18);
    ASSERT_TRUE(plan.ok()) << plan.error().message;
    const auto& p = plan.value();
    EXPECT_DOUBLE_EQ(p.doorWidth, 897);   // (1764 + 1 * 30) / 2
    EXPECT_DOUBLE_EQ(p.doorHeight, 2324); // 2364 - 40
    EXPECT_DOUBLE_EQ(p.interiorFrontY, 95); // 10 + 80 + 5
    EXPECT_DOUBLE_EQ(p.trackX1 - p.trackX0, 1764);
    ASSERT_EQ(p.doors.size(), 2u);
    EXPECT_DOUBLE_EQ(p.doors[0].x0, 18);
    EXPECT_DOUBLE_EQ(p.doors[0].x1, 915);  // 18 + 897
    EXPECT_DOUBLE_EQ(p.doors[1].x0, 885);  // 18 + (897 - 30)
    EXPECT_DOUBLE_EQ(p.doors[1].x1, 1782);
    EXPECT_EQ(p.doors[0].plane, 1); // back lane
    EXPECT_EQ(p.doors[1].plane, 0); // front lane
    EXPECT_DOUBLE_EQ(p.doors[1].yFront, 20); // setback 10 + front offset 10
    EXPECT_DOUBLE_EQ(p.doors[0].yFront, 55); // + pitch 35
    EXPECT_DOUBLE_EQ(p.doors[0].yBack, 73);
    EXPECT_DOUBLE_EQ(p.doors[0].z0, 33);   // 18 + 15
    EXPECT_DOUBLE_EQ(p.doors[0].z1, 2357); // 33 + 2324 (25 mm below the top)
}

TEST(SlidingDoors, ThreeDoorPlanAlternatesPlanes) {
    auto plan = planSlidingDoors(def(ids::kSlidingGeneric), wardrobe(), 3, 18);
    ASSERT_TRUE(plan.ok()) << plan.error().message;
    const auto& p = plan.value();
    EXPECT_DOUBLE_EQ(p.doorWidth, 608); // (1764 + 2 * 30) / 3
    ASSERT_EQ(p.doors.size(), 3u);
    const double x0[] = {18, 596, 1174}; // 18 + i * 578
    const int plane[] = {1, 0, 1};
    for (int i = 0; i < 3; ++i) {
        EXPECT_DOUBLE_EQ(p.doors[i].x0, x0[i]);
        EXPECT_NEAR(p.doors[i].x1 - p.doors[i].x0, 608, 1e-9);
        EXPECT_EQ(p.doors[i].plane, plane[i]);
    }
    // Doors 1 and 3 share the back lane and leave 1174 - 626 = 548 mm between them.
    EXPECT_GT(p.doors[2].x0, p.doors[0].x1);
}

TEST(SlidingDoors, PlanErrors) {
    const auto sys = def(ids::kSlidingGeneric);
    auto r = planSlidingDoors(sys, wardrobe(300), 2, 18); // 300 - 95 = 205 < 250
    ASSERT_FALSE(r.ok());
    EXPECT_NE(r.error().message.find("too shallow"), std::string::npos) << r.error().message;
    EXPECT_NE(r.error().message.find("205"), std::string::npos) << r.error().message;
    r = planSlidingDoors(sys, wardrobe(90), 2, 18); // < 95
    ASSERT_FALSE(r.ok());
    EXPECT_NE(r.error().message.find("too shallow for the tracks"), std::string::npos) << r.error().message;
    r = planSlidingDoors(sys, wardrobe(), 6, 18); // (1764 + 150) / 6 = 319 < 400
    ASSERT_FALSE(r.ok());
    EXPECT_NE(r.error().message.find("too narrow"), std::string::npos) << r.error().message;
    EXPECT_NE(r.error().message.find("319"), std::string::npos) << r.error().message;
    r = planSlidingDoors(sys, {0, 2600, 0, 2000, 600}, 2, 18); // (2600 + 30) / 2 = 1315 > 1200
    ASSERT_FALSE(r.ok());
    EXPECT_NE(r.error().message.find("too wide"), std::string::npos);
    r = planSlidingDoors(sys, wardrobe(), 1, 18);
    ASSERT_FALSE(r.ok());
    r = planSlidingDoors(sys, wardrobe(), 2, 36); // >= lane pitch 35
    ASSERT_FALSE(r.ok());
    EXPECT_NE(r.error().message.find("too thick"), std::string::npos);
    r = planSlidingDoors(def(ids::kLiftGeneric), wardrobe(), 2, 18);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, core::ErrorCode::InvalidArgument);
}

TEST(SlidingDoors, TrackScrewHoles) {
    EXPECT_EQ(slidingTrackScrewCount(1764, 50, 300), 7); // ceil(1664 / 300) + 1
    EXPECT_EQ(slidingTrackScrewCount(700, 50, 300), 3);  // ceil(600 / 300) + 1
    EXPECT_EQ(slidingTrackScrewCount(90, 50, 300), 0);
    // Top panel x 18..1782, y 0..600, z 2382..2400; bottom panel z 0..18.
    const auto top = makePart("w/top", "Top", 1764, 600, 18, Transform::translation({18, 0, 2382}));
    const auto bottom = makePart("w/bottom", "Bottom", 1764, 600, 18, Transform::translation({18, 0, 0}));
    SlidingTrackConfig c;
    c.instanceId = domain::HardwareInstanceId("w/hw.track.top");
    c.x0 = 18;
    c.x1 = 1782;
    c.y = 50; // 10 + 80 / 2
    auto r = placeSlidingTrack(top, def(ids::kSlidingGeneric), c);
    ASSERT_TRUE(r.ok()) << r.error().message;
    const auto* ops = r.value().operationsFor(top.id);
    ASSERT_NE(ops, nullptr);
    ASSERT_EQ(ops->size(), 7u);
    for (std::size_t k = 0; k < 7; ++k) {
        const auto pt = holePoint(top, (*ops)[k]);
        EXPECT_NEAR(pt.x, 68 + static_cast<double>(k) * (1664.0 / 6), 1e-6); // 18 + 50, step 277.33
        EXPECT_NEAR(pt.y, 50, 1e-9);
        EXPECT_NEAR(pt.z, 2382, 1e-9); // underside
        EXPECT_DOUBLE_EQ(std::get<Hole>((*ops)[k].shape).depth, 10);
    }
    EXPECT_EQ((*ops)[0].id.str(), "w/hw.track.top/op.1");
    EXPECT_NE(r.value().instances[0].note.find("1764"), std::string::npos);
    c.top = false;
    c.instanceId = domain::HardwareInstanceId("w/hw.track.bottom");
    r = placeSlidingTrack(bottom, def(ids::kSlidingGeneric), c);
    ASSERT_TRUE(r.ok()) << r.error().message;
    EXPECT_NEAR(holePoint(bottom, r.value().operations[0].operations[0]).z, 18, 1e-9); // upper face
    // A vertical panel cannot carry a track; a shared-panel limit shortens the holes.
    const auto side = makePart("w/side", "Side", 2400, 600, 18, Transform::fromAxes(Z, Y, {18, 0, 0}));
    EXPECT_FALSE(placeSlidingTrack(side, def(ids::kSlidingGeneric), c).ok());
    c.maxHoleDepth = 8;
    r = placeSlidingTrack(bottom, def(ids::kSlidingGeneric), c);
    ASSERT_TRUE(r.ok());
    EXPECT_DOUBLE_EQ(std::get<Hole>(r.value().operations[0].operations[0].shape).depth, 8);
}

TEST(SlidingDoors, RollerHoles) {
    // Back-lane door of the 2-door plan: x 18..915, y 55..73, z 33..2357.
    const auto door = makePart("w/sliding.1", "Sliding door 1", 2324, 897, 18, Transform::fromAxes(Z, X, {18, 55, 33}));
    SlidingRollerConfig c;
    c.instanceId = domain::HardwareInstanceId("w/hw.rollers.sliding.1");
    auto r = placeSlidingDoorRollers(door, def(ids::kSlidingGeneric), c);
    ASSERT_TRUE(r.ok()) << r.error().message;
    const auto& ops = r.value().operations.at(0).operations;
    ASSERT_EQ(ops.size(), 4u);
    const double xs[] = {58, 875, 58, 875};   // 18 + 40, 915 - 40
    const double zs[] = {63, 63, 2327, 2327}; // 33 + 30, 2357 - 30
    for (std::size_t k = 0; k < 4; ++k) {
        const auto pt = holePoint(door, ops[k]);
        EXPECT_NEAR(pt.x, xs[k], 1e-9);
        EXPECT_NEAR(pt.y, 73, 1e-9); // rear face
        EXPECT_NEAR(pt.z, zs[k], 1e-9);
        const auto& h = std::get<Hole>(ops[k].shape);
        EXPECT_EQ(h.face, Face::A);
        EXPECT_DOUBLE_EQ(h.diameter, 4);
        EXPECT_DOUBLE_EQ(h.depth, 10);
    }
    // 12 mm door: 10 mm holes leave 2 mm, not more than 2.
    const auto thin = makePart("w/thin", "Thin door", 2324, 897, 12, Transform::fromAxes(Z, X, {18, 55, 33}));
    EXPECT_FALSE(placeSlidingDoorRollers(thin, def(ids::kSlidingGeneric), c).ok());
}
