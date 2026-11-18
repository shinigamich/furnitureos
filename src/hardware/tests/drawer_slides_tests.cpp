// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/hardware/drawer_slides.hpp"
#include "test_util.hpp"

using namespace fos;
using namespace fos::hardware;
using namespace fos::hardware::testing;
using geometry::Transform;

namespace {
/// Box side 500 x 150 x 16: local x -> asm +Y, local y -> asm +Z, thickness -> asm +X.
/// Placed 12.7 mm right of the reference cabinet's left side (inner face x = 18): x 30.7..46.7,
/// y 0..500, z 100..250. Face B (x = 30.7) looks at the cabinet side.
domain::Part boxSide(double x = 18 + 12.7) {
    return makePart("drw/side.left", "Drawer side", 500, 150, 16, Transform::fromAxes({0, 1, 0}, {0, 0, 1}, {x, 0, 100}));
}
} // namespace

TEST(DrawerSlides, SizingRules) {
    const auto bb = def(ids::kSlideBallBearing), um = def(ids::kSlideUndermount);
    // Lengths 250, 300, ..., 650 (9 values).
    ASSERT_EQ(slideNominalLengths(bb).value().size(), 9u);
    // Depth 550: largest L with L + 0 <= 550 -> 550; undermount L + 3 <= 550 -> 500.
    EXPECT_DOUBLE_EQ(selectSlideLength(bb, 550).value(), 550);
    EXPECT_DOUBLE_EQ(selectSlideLength(um, 550).value(), 500);
    // 564 mm opening: ball-bearing 564 - 2 * 12.7 = 538.6; undermount 564 - 42 + 2 * 16 = 554.
    EXPECT_NEAR(drawerBoxOuterWidth(bb, 564, 16).value(), 538.6, 1e-9);
    EXPECT_NEAR(drawerBoxOuterWidth(um, 564, 16).value(), 554, 1e-9);
    EXPECT_DOUBLE_EQ(drawerBoxLength(um, 500).value(), 490);
    // Failure paths: too shallow (249 < 250), sides too thick for the undermount runner.
    const auto shallow = selectSlideLength(bb, 249);
    ASSERT_FALSE(shallow);
    EXPECT_NE(shallow.error().message.find("too shallow for the shortest drawer slide"), std::string::npos);
    EXPECT_FALSE(drawerBoxOuterWidth(um, 564, 18));
    EXPECT_FALSE(selectSlideLength(def(ids::kShelfPin5), 500)); // wrong kind
}

TEST(DrawerSlides, BallBearingHolesOnBothParts) {
    Cabinet c;
    const auto side = boxSide();
    DrawerSlideConfig cfg;
    cfg.instanceId = domain::HardwareInstanceId("drw/hw.slide.left");
    cfg.slideLength = 500;
    const auto r = placeDrawerSlides(side, c.left, def(ids::kSlideBallBearing), cfg);
    ASSERT_TRUE(r) << r.error().message;
    // Cabinet member: 37, 165, 293, 421 from the front (<= 500 - 20 = 480), line 100 + 22.5.
    // Left side face B: u = asm z, v = asm y.
    const auto* cab = r.value().operationsFor(c.left.id);
    ASSERT_NE(cab, nullptr);
    ASSERT_EQ(cab->size(), 4u);
    const double cabPos[] = {37, 165, 293, 421};
    for (std::size_t i = 0; i < 4; ++i) expectHole((*cab)[i], {domain::Face::B, 122.5, cabPos[i], 5, 12, false});
    // Drawer member: 32, 160, 288, 416 on the outer face B of the box side (u = asm y, v = height above underside).
    const auto* drw = r.value().operationsFor(side.id);
    ASSERT_NE(drw, nullptr);
    ASSERT_EQ(drw->size(), 4u);
    const double drwPos[] = {32, 160, 288, 416};
    for (std::size_t i = 0; i < 4; ++i) expectHole((*drw)[i], {domain::Face::B, drwPos[i], 22.5, 2.5, 8, false});
    EXPECT_EQ((*drw)[0].id.str(), "drw/hw.slide.left/op.5");
}

TEST(DrawerSlides, Errors) {
    Cabinet c;
    DrawerSlideConfig cfg;
    cfg.instanceId = domain::HardwareInstanceId("drw/hw.slide.left");
    cfg.slideLength = 500;
    // Gap 7 mm instead of 12.7.
    const auto gap = placeDrawerSlides(boxSide(25), c.left, def(ids::kSlideBallBearing), cfg);
    ASSERT_FALSE(gap);
    EXPECT_NE(gap.error().message.find("is 7 mm but drawer slide"), std::string::npos) << gap.error().message;
    // 520 is not a nominal length.
    cfg.slideLength = 520;
    EXPECT_FALSE(placeDrawerSlides(boxSide(), c.left, def(ids::kSlideBallBearing), cfg));
    // A 600 mm slide runs past the 560 mm deep side.
    cfg.slideLength = 600;
    const auto longSlide = placeDrawerSlides(boxSide(), c.left, def(ids::kSlideBallBearing), cfg);
    ASSERT_FALSE(longSlide);
    EXPECT_NE(longSlide.error().message.find("runs outside"), std::string::npos);
}
