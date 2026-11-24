// SPDX-License-Identifier: AGPL-3.0-only
// Generic concealed cabinet hanger and wall rail: holes, rail instance, errors.
#include "fos/hardware/cabinet_hanger.hpp"
#include "test_util.hpp"

#include <gmock/gmock.h>

using namespace fos;
using namespace fos::hardware;
using namespace fos::hardware::testing;
using domain::Face;
using domain::Hole;
using ::testing::HasSubstr;

namespace {
CabinetHangerConfig cfg(const char* id) {
    CabinetHangerConfig c;
    c.instanceId = domain::HardwareInstanceId(id);
    return c;
}
} // namespace

TEST(CabinetHanger, CatalogueEntries) {
    const auto h = def(ids::kHangerGeneric);
    EXPECT_EQ(h.kind, domain::HardwareKind::Suspension);
    EXPECT_FALSE(h.dimensionsVerified);
    EXPECT_DOUBLE_EQ(h.params.at(keys::kBackSpaceMin), 18);
    EXPECT_DOUBLE_EQ(hangerBackSpace(h).value(), 18);
    const auto r = def(ids::kWallRailGeneric);
    EXPECT_EQ(r.kind, domain::HardwareKind::Other);
    EXPECT_FALSE(r.dimensionsVerified);
    EXPECT_EQ(r.custom.at("unit"), "m");
    EXPECT_DOUBLE_EQ(r.params.at(keys::kRailThickness), 12);
}

// Fixture (test_util.hpp): left side x 0..18, y 0..560, z 0..720, inner face B, face (u, v) =
// (asm z, asm y); right side inner face A, same axes; top z 702..720 -> reference z = 702.
//   screw column y = 560 - 8 = 552; heights z = 702 - 15 = 687 and 687 - 32 = 655.
TEST(CabinetHanger, HolesInTopRearCornerOfInnerFace) {
    Cabinet c;
    const auto h = def(ids::kHangerGeneric);
    auto l = placeCabinetHanger(c.left, c.top, h, cfg("cab1/hanger.left"));
    ASSERT_TRUE(l.ok()) << l.error().message;
    const auto* ops = l.value().operationsFor(c.left.id);
    ASSERT_NE(ops, nullptr);
    ASSERT_EQ(ops->size(), 2u);
    expectHole((*ops)[0], Hole{Face::B, 687, 552, 3, 10, false});
    expectHole((*ops)[1], Hole{Face::B, 655, 552, 3, 10, false});
    EXPECT_EQ((*ops)[0].id.str(), "cab1/hanger.left/op.1");
    ASSERT_EQ(l.value().instances.size(), 1u);
    EXPECT_EQ(l.value().instances[0].quantity, 1);
    expectVec(l.value().instances[0].position, {18, 552, 687});

    auto r = placeCabinetHanger(c.right, c.top, h, cfg("cab1/hanger.right"));
    ASSERT_TRUE(r.ok()) << r.error().message;
    const auto* rops = r.value().operationsFor(c.right.id);
    ASSERT_NE(rops, nullptr);
    expectHole((*rops)[0], Hole{Face::A, 687, 552, 3, 10, false});
    expectHole((*rops)[1], Hole{Face::A, 655, 552, 3, 10, false});
    expectVec(r.value().instances[0].position, {782, 552, 687});
}

TEST(CabinetHanger, Errors) {
    Cabinet c;
    const auto h = def(ids::kHangerGeneric);
    // Wrong kind.
    auto wrong = placeCabinetHanger(c.left, c.top, def(ids::kShelfPin5), cfg("x"));
    ASSERT_FALSE(wrong.ok());
    EXPECT_EQ(wrong.error().code, core::ErrorCode::InvalidArgument);
    // 10 mm deep screws in a 12 mm side leave 2 mm, not more than 2 -> refused.
    domain::Part thin = c.left;
    thin.thickness = 12;
    auto t = placeCabinetHanger(thin, c.top, h, cfg("x"));
    ASSERT_FALSE(t.ok());
    EXPECT_THAT(t.error().message, HasSubstr("would leave 2"));
    // A horizontal panel is not a side.
    auto flat = placeCabinetHanger(c.bottom, c.top, h, cfg("x"));
    ASSERT_FALSE(flat.ok());
    // Inconsistent definition: back_space_min below body_depth.
    auto bad = h;
    bad.params[keys::kBackSpaceMin] = 10;
    EXPECT_FALSE(hangerBackSpace(bad).ok());
    // Empty instance id.
    EXPECT_FALSE(placeCabinetHanger(c.left, c.top, h, CabinetHangerConfig{}).ok());
}

// Rail for the 800 mm fixture cabinet: x 0..800 on the wall plane y = 560; top edge
// 702 - 20 = 682.
TEST(WallRail, InstanceWithLengthNote) {
    Cabinet c;
    WallRailConfig rc;
    rc.instanceId = domain::HardwareInstanceId("cab1/wall_rail");
    rc.x0 = 0;
    rc.x1 = 800;
    rc.wallY = 560;
    rc.referenceZ = 702;
    rc.parts = {c.left.id, c.right.id};
    auto r = placeWallRail(def(ids::kWallRailGeneric), def(ids::kHangerGeneric), rc);
    ASSERT_TRUE(r.ok()) << r.error().message;
    ASSERT_EQ(r.value().instances.size(), 1u);
    EXPECT_TRUE(r.value().operations.empty());
    const auto& inst = r.value().instances[0];
    EXPECT_EQ(inst.definition.str(), ids::kWallRailGeneric);
    EXPECT_EQ(inst.quantity, 1);
    expectVec(inst.position, {0, 560, 682});
    EXPECT_THAT(inst.note, HasSubstr("Wall rail 800 mm (cut on site)"));

    // An 18 mm rail does not fit into the 18 mm space behind the back panel.
    auto thick = def(ids::kWallRailGeneric);
    thick.params[keys::kRailThickness] = 18;
    auto e = placeWallRail(thick, def(ids::kHangerGeneric), rc);
    ASSERT_FALSE(e.ok());
    EXPECT_EQ(e.error().code, core::ErrorCode::ConstraintViolation);
    // Zero length.
    rc.x1 = 0;
    EXPECT_FALSE(placeWallRail(def(ids::kWallRailGeneric), def(ids::kHangerGeneric), rc).ok());
}
