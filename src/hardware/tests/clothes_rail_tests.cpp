// SPDX-License-Identifier: AGPL-3.0-only
// Generic wardrobe (clothes) rail and its end supports: catalogue, space checks, holes, instances.
#include "fos/hardware/clothes_rail.hpp"
#include "test_util.hpp"

#include <gmock/gmock.h>

using namespace fos;
using namespace fos::hardware;
using namespace fos::hardware::testing;
using domain::Face;
using domain::Hole;
using ::testing::HasSubstr;

namespace {
/// Section of the reference cabinet: clear depth y 0..560, clear height z 18..702.
ClothesRailSpace cabinetSpace(double fromTop = 60) { return {0, 560, 18, 702, fromTop}; }
ClothesRailConfig cfg(ClothesRailSpace space) {
    ClothesRailConfig c;
    c.railInstanceId = domain::HardwareInstanceId("cab1/hw.rail");
    c.supportInstanceId = domain::HardwareInstanceId("cab1/hw.rail_supports");
    c.space = space;
    return c;
}
/// The generic rail with hanging_space_min lowered so that it fits the 720 mm reference cabinet.
domain::HardwareDefinition lowRail() {
    auto r = def(ids::kClothesRailGeneric);
    r.params[keys::kHangingSpaceMin] = 600;
    return r;
}
} // namespace

TEST(ClothesRail, CatalogueEntries) {
    const auto r = def(ids::kClothesRailGeneric);
    EXPECT_EQ(r.kind, domain::HardwareKind::Other);
    EXPECT_FALSE(r.dimensionsVerified);
    EXPECT_EQ(r.custom.at("unit"), "m");
    EXPECT_DOUBLE_EQ(r.params.at(keys::kRailHeight), 30);
    EXPECT_DOUBLE_EQ(r.params.at(keys::kRailThickness), 15);
    EXPECT_DOUBLE_EQ(r.params.at(keys::kHookClearance), 35);
    EXPECT_DOUBLE_EQ(r.params.at(keys::kSectionDepthMin), 500);
    EXPECT_DOUBLE_EQ(r.params.at(keys::kHangingSpaceMin), 800);
    const auto s = def(ids::kRailSupportGeneric);
    EXPECT_EQ(s.kind, domain::HardwareKind::Other);
    EXPECT_FALSE(s.dimensionsVerified);
    EXPECT_EQ(s.custom.count("unit"), 0u);
    EXPECT_DOUBLE_EQ(s.params.at(keys::kBodyHeight), 50);
    EXPECT_DOUBLE_EQ(s.params.at(keys::kScrewSpacing), 32);
}

// Axis y = (0 + 560) / 2 = 280; z = 702 - 60 = 642; 642 - 18 = 624 mm below the axis.
TEST(ClothesRail, PlanAxisAndSpaceLimits) {
    const auto rail = def(ids::kClothesRailGeneric), sup = def(ids::kRailSupportGeneric);
    auto ok = planClothesRail(lowRail(), sup, cabinetSpace());
    ASSERT_TRUE(ok.ok()) << ok.error().message;
    EXPECT_DOUBLE_EQ(ok.value().y, 280);
    EXPECT_DOUBLE_EQ(ok.value().z, 642);
    // Generic hanging_space_min 800 > 624 -> too low.
    auto low = planClothesRail(rail, sup, cabinetSpace());
    ASSERT_FALSE(low.ok());
    EXPECT_EQ(low.error().code, core::ErrorCode::ConstraintViolation);
    EXPECT_THAT(low.error().message, HasSubstr("too low"));
    EXPECT_THAT(low.error().message, HasSubstr("only 624 mm remain"));
    // 450 mm deep < section_depth_min 500 -> too shallow.
    auto shallow = planClothesRail(lowRail(), sup, {0, 450, 18, 702, 60});
    ASSERT_FALSE(shallow.ok());
    EXPECT_THAT(shallow.error().message, HasSubstr("only 450 mm deep"));
    EXPECT_THAT(shallow.error().message, HasSubstr("at least 500 mm"));
    // Minimum below the top = max(50 / 2, 30 / 2 + 35) = 50 mm; 40 is refused, 50 accepted.
    auto high = planClothesRail(lowRail(), sup, cabinetSpace(40));
    ASSERT_FALSE(high.ok());
    EXPECT_THAT(high.error().message, HasSubstr("at least 50 mm"));
    EXPECT_TRUE(planClothesRail(lowRail(), sup, cabinetSpace(50)).ok());
    // Screws outside the support body: 48 / 2 + 3 / 2 = 25.5 > 50 / 2.
    auto bad = sup;
    bad.params[keys::kScrewSpacing] = 48;
    auto inv = planClothesRail(lowRail(), bad, cabinetSpace());
    ASSERT_FALSE(inv.ok());
    EXPECT_EQ(inv.error().code, core::ErrorCode::InvalidArgument);
}

// Left side inner face B, right side inner face A, face (u, v) = (asm z, asm y) on both.
// Holes at y = 280, z = 642 +- 32 / 2 = 658 and 626. Clear width = 782 - 18 = 764.
TEST(ClothesRail, SupportsHolesAndRailInstance) {
    Cabinet c;
    auto p = placeClothesRail(c.left, c.right, lowRail(), def(ids::kRailSupportGeneric), cfg(cabinetSpace()));
    ASSERT_TRUE(p.ok()) << p.error().message;
    const auto& pl = p.value();
    ASSERT_EQ(pl.instances.size(), 2u);
    const auto& rail = pl.instances[0];
    EXPECT_EQ(rail.id.str(), "cab1/hw.rail");
    EXPECT_EQ(rail.definition.str(), ids::kClothesRailGeneric);
    EXPECT_EQ(rail.quantity, 1);
    EXPECT_DOUBLE_EQ(rail.length, 764);
    expectVec(rail.position, {18, 280, 642});
    EXPECT_THAT(rail.note, HasSubstr("Clothes rail 764 mm"));
    const auto& sup = pl.instances[1];
    EXPECT_EQ(sup.quantity, 2);
    EXPECT_EQ(sup.parts.size(), 2u);
    expectVec(sup.position, {18, 280, 658});
    const auto* l = pl.operationsFor(c.left.id);
    const auto* r = pl.operationsFor(c.right.id);
    ASSERT_NE(l, nullptr);
    ASSERT_NE(r, nullptr);
    ASSERT_EQ(l->size(), 2u);
    ASSERT_EQ(r->size(), 2u);
    expectHole((*l)[0], Hole{Face::B, 658, 280, 3, 10, false});
    expectHole((*l)[1], Hole{Face::B, 626, 280, 3, 10, false});
    expectHole((*r)[0], Hole{Face::A, 658, 280, 3, 10, false});
    expectHole((*r)[1], Hole{Face::A, 626, 280, 3, 10, false});
    EXPECT_EQ((*l)[0].id.str(), "cab1/hw.rail_supports/op.1");
    EXPECT_EQ((*r)[1].id.str(), "cab1/hw.rail_supports/op.4");
    EXPECT_EQ((*r)[1].source, "cab1/hw.rail_supports");
    // Coaxial across the cabinet: same (u, v) on facing faces.
    EXPECT_TRUE(coaxial(c.left, hole((*l)[0]), c.right, hole((*r)[0])));
}

TEST(ClothesRail, PlacementErrorsAndDepthLimit) {
    Cabinet c;
    const auto sup = def(ids::kRailSupportGeneric);
    // Shared-panel limit on the right: 8 mm instead of 10.
    auto lim = cfg(cabinetSpace());
    lim.maxScrewDepthRight = 8;
    auto p = placeClothesRail(c.left, c.right, lowRail(), sup, lim);
    ASSERT_TRUE(p.ok()) << p.error().message;
    EXPECT_DOUBLE_EQ(hole((*p.value().operationsFor(c.right.id))[0]).depth, 8);
    EXPECT_DOUBLE_EQ(hole((*p.value().operationsFor(c.left.id))[0]).depth, 10);
    // 10 mm screws in a 12 mm panel leave 2 mm, not more than 2.
    domain::Part thin = c.right;
    thin.thickness = 12;
    auto t = placeClothesRail(c.left, thin, lowRail(), sup, cfg(cabinetSpace()));
    ASSERT_FALSE(t.ok());
    EXPECT_THAT(t.error().message, HasSubstr("would leave 2"));
    // Swapped panels do not face each other.
    auto sw = placeClothesRail(c.right, c.left, lowRail(), sup, cfg(cabinetSpace()));
    ASSERT_FALSE(sw.ok());
    EXPECT_THAT(sw.error().message, HasSubstr("do not face each other"));
    // A horizontal panel cannot carry a support.
    auto flat = placeClothesRail(c.bottom, c.right, lowRail(), sup, cfg(cabinetSpace()));
    ASSERT_FALSE(flat.ok());
    EXPECT_EQ(flat.error().code, core::ErrorCode::ConstraintViolation);
    // Same instance id twice.
    auto same = cfg(cabinetSpace());
    same.supportInstanceId = same.railInstanceId;
    auto s = placeClothesRail(c.left, c.right, lowRail(), sup, same);
    ASSERT_FALSE(s.ok());
    EXPECT_EQ(s.error().code, core::ErrorCode::InvalidArgument);
    // Space errors are passed through (generic rail, 624 mm below the axis).
    auto low = placeClothesRail(c.left, c.right, def(ids::kClothesRailGeneric), sup, cfg(cabinetSpace()));
    ASSERT_FALSE(low.ok());
    EXPECT_THAT(low.error().message, HasSubstr("too low"));
}
