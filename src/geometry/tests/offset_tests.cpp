// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/geometry/offset.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <string>

using namespace fos::geometry;
using fos::core::ErrorCode;

namespace {

Polygon2 rect100x50() { return makeRectPolygon(Rect2::fromSize(100, 50)); }
Polygon2 lShape() { return {{{0, 0}, {100, 0}, {100, 40}, {40, 40}, {40, 100}, {0, 100}}}; }
/// Right triangle with legs 40 and 30 (hypotenuse 50, inradius (40 + 30 - 50) / 2 = 10, incentre (10,10)).
Polygon2 triangle345() { return {{{0, 0}, {40, 0}, {0, 30}}}; }

void expectPolygonNear(const Polygon2& actual, const Polygon2& expected, double tol = 1e-9) {
    ASSERT_EQ(actual.size(), expected.size());
    for (std::size_t i = 0; i < actual.size(); ++i)
        EXPECT_TRUE(approxEqual(actual[i], expected[i], tol))
            << "vertex " << i << ": (" << actual[i].x << ", " << actual[i].y << ") expected (" << expected[i].x
            << ", " << expected[i].y << ")";
}

} // namespace

TEST(Offset, RectangleInflate) {
    const auto r = offsetPolygon(rect100x50(), 5.0);
    ASSERT_TRUE(r.ok());
    // 100 x 50 grown by 5 on every side -> 110 x 60 with mitered (square) corners.
    expectPolygonNear(r.value(), {{{-5, -5}, {105, -5}, {105, 55}, {-5, 55}}});
    EXPECT_NEAR(bounds(r.value()).width(), 110.0, 1e-9);
    EXPECT_NEAR(bounds(r.value()).height(), 60.0, 1e-9);
    EXPECT_NEAR(signedArea(r.value()), 6600.0, 1e-6);
}

TEST(Offset, RectangleDeflate) {
    const auto r = offsetPolygon(rect100x50(), -10.0);
    ASSERT_TRUE(r.ok());
    expectPolygonNear(r.value(), {{{10, 10}, {90, 10}, {90, 40}, {10, 40}}}); // 80 x 30
    const auto thin = offsetPolygon(rect100x50(), -24.0);
    ASSERT_TRUE(thin.ok());
    expectPolygonNear(thin.value(), {{{24, 24}, {76, 24}, {76, 26}, {24, 26}}}); // 52 x 2
}

TEST(Offset, RectangleDeflateBeyondCollapseIsAnError) {
    // Height 50: deflating by 30 would need a height of -10.
    const auto r = offsetPolygon(rect100x50(), -30.0);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, ErrorCode::ConstraintViolation);
    EXPECT_NE(r.error().message.find("-30 mm"), std::string::npos) << r.error().message;
    EXPECT_NE(r.error().message.find("100 x 50 mm"), std::string::npos) << r.error().message;
    // Exactly half the height: a zero-area line remains -> also an error.
    EXPECT_FALSE(offsetPolygon(rect100x50(), -25.0).ok());
    EXPECT_FALSE(offsetPolygon(rect100x50(), -1000.0).ok());
}

TEST(Offset, ZeroDistanceAndClockwiseInput) {
    const auto same = offsetPolygon(rect100x50(), 0.0);
    ASSERT_TRUE(same.ok());
    EXPECT_EQ(same.value(), rect100x50());
    // Clockwise input with a redundant collinear vertex: positive still means "grow" and the
    // result is counter-clockwise, still starting from the corner next to input vertex 0.
    const Polygon2 cw{{{0, 0}, {0, 50}, {50, 50}, {100, 50}, {100, 0}}};
    const auto r = offsetPolygon(cw, 5.0);
    ASSERT_TRUE(r.ok());
    expectPolygonNear(r.value(), {{{-5, -5}, {105, -5}, {105, 55}, {-5, 55}}});
    EXPECT_EQ(winding(r.value()), Winding::CounterClockwise);
    const auto d = offsetPolygon(cw, -10.0);
    ASSERT_TRUE(d.ok());
    EXPECT_NEAR(signedArea(d.value()), 80.0 * 30.0, 1e-6);
}

TEST(Offset, TriangleMiterWithinLimit) {
    // With a generous miter limit the result is the similar triangle about the incentre
    // (10,10): inradius 10 -> 15, scale 1.5: V' = I + 1.5 (V - I).
    //   (0,0) -> (-5,-5); (40,0) -> (10 + 45, 10 - 15) = (55,-5); (0,30) -> (10 - 15, 10 + 30) = (-5,40).
    OffsetOptions opt;
    opt.miterLimit = 4.0;
    const auto r = offsetPolygon(triangle345(), 5.0, opt);
    ASSERT_TRUE(r.ok());
    expectPolygonNear(r.value(), {{{-5, -5}, {55, -5}, {-5, 40}}});
    EXPECT_NEAR(signedArea(r.value()), 600.0 * 2.25, 1e-6);
}

TEST(Offset, TriangleSharpCornersAreBevelledBeyondMiterLimit) {
    // Miter ratio = 1 / sin(interior angle / 2):
    //   (0,0): 90 deg        -> 1.414 (miter)
    //   (40,0): 36.87 deg    -> 1 / sin(18.435 deg) = sqrt(10) = 3.162 > 2 (bevel)
    //   (0,30): 53.13 deg    -> 1 / sin(26.565 deg) = sqrt(5)  = 2.236 > 2 (bevel)
    // Outward normals: bottom (0,-1), hypotenuse (0.6,0.8), left (-1,0); hypotenuse direction
    // (-0.8,0.6). A bevel cuts at distance d = 5 from the vertex, perpendicular to the bisector.
    // With alpha = angle between an edge normal and the bisector (cos alpha = 1 / ratio), the
    // cut meets each offset edge line d * tan(alpha / 2) beyond the foot point (vertex + d * normal),
    // tan(alpha / 2) = (1 - cos alpha) / sin alpha:
    //   (40,0): cos = 1/sqrt(10), sin = 3/sqrt(10) -> t1 = (sqrt(10) - 1) / 3 = 0.7208
    //           foot (40,-5) moved along +X: (40 + 5 t1, -5);
    //           foot (43,4) moved against the hypotenuse direction: (43 + 4 t1, 4 - 3 t1)
    //   (0,30): cos = 1/sqrt(5), sin = 2/sqrt(5)   -> t2 = (sqrt(5) - 1) / 2 = 0.6180
    //           foot (3,34) moved along the hypotenuse direction: (3 - 4 t2, 34 + 3 t2);
    //           foot (-5,30) moved along +Y: (-5, 30 + 5 t2)
    const double t1 = (std::sqrt(10.0) - 1.0) / 3.0, t2 = (std::sqrt(5.0) - 1.0) / 2.0;
    const auto r = offsetPolygon(triangle345(), 5.0);
    ASSERT_TRUE(r.ok());
    expectPolygonNear(r.value(), {{{-5, -5}, {40 + 5 * t1, -5}, {43 + 4 * t1, 4 - 3 * t1},
                                   {3 - 4 * t2, 34 + 3 * t2}, {-5, 30 + 5 * t2}}});
    // Area = mitered triangle 1350 minus the two cut-off tips. A tip is an isosceles triangle
    // with height d (ratio - 1) and half base d * tan(alpha / 2):
    //   25 (sqrt(10) - 1)^2 / 3 = 38.962 and 25 (sqrt(5) - 1)^2 / 2 = 19.098  ->  1291.940
    EXPECT_NEAR(signedArea(r.value()),
                1350.0 - 25.0 * (11.0 - 2.0 * std::sqrt(10.0)) / 3.0 - 25.0 * (3.0 - std::sqrt(5.0)), 1e-6);
    EXPECT_NEAR(signedArea(r.value()), 1291.940, 1e-3);
    // The bevel never comes closer than 5 mm to the original outline.
    EXPECT_NEAR(polygonDistance(Polygon2{{r.value()[1], r.value()[2], r.value()[2]}}, triangle345()), 5.0, 1e-9);
    // Limit 2.5 lies between 2.236 and 3.162: only the (0,30) corner is mitered, tip (-5,40).
    OffsetOptions opt;
    opt.miterLimit = 2.5;
    const auto partly = offsetPolygon(triangle345(), 5.0, opt);
    ASSERT_TRUE(partly.ok());
    expectPolygonNear(partly.value(), {{{-5, -5}, {40 + 5 * t1, -5}, {43 + 4 * t1, 4 - 3 * t1}, {-5, 40}}});
}

TEST(Offset, TriangleDeflateAndCollapse) {
    // Inradius 10 -> 5, scale 0.5 about (10,10): (5,5), (25,5), (5,20).
    const auto r = offsetPolygon(triangle345(), -5.0);
    ASSERT_TRUE(r.ok());
    expectPolygonNear(r.value(), {{{5, 5}, {25, 5}, {5, 20}}});
    EXPECT_TRUE(offsetPolygon(triangle345(), -9.9).ok());
    // Deflating by the inradius leaves a single point.
    const auto gone = offsetPolygon(triangle345(), -10.0);
    ASSERT_FALSE(gone.ok());
    EXPECT_EQ(gone.error().code, ErrorCode::ConstraintViolation);
    EXPECT_FALSE(offsetPolygon(triangle345(), -12.0).ok());
}

TEST(Offset, ConvexDeflateWhereAnEdgeVanishes) {
    // 100 x 50 rectangle with a 2 mm chamfer at the top-right corner. Deflating by 10 moves
    // the chamfer line x + y = 148 to x + y = 148 - 10*sqrt(2) = 133.86, beyond the new corner
    // (90,40) (sum 130): the chamfer edge disappears and a plain 80 x 30 rectangle remains.
    const Polygon2 chamfered{{{0, 0}, {100, 0}, {100, 48}, {98, 50}, {0, 50}}};
    ASSERT_TRUE(isConvex(chamfered));
    const auto r = offsetPolygon(chamfered, -10.0);
    ASSERT_TRUE(r.ok());
    expectPolygonNear(r.value(), {{{10, 10}, {90, 10}, {90, 40}, {10, 40}}});
    // Deflating by 1 keeps the chamfer: line x + y = 148 - sqrt(2) cuts x = 99 at y = 47.586
    // and y = 49 at x = 97.586.
    const auto s = offsetPolygon(chamfered, -1.0);
    ASSERT_TRUE(s.ok());
    const double k = 148.0 - std::sqrt(2.0);
    expectPolygonNear(s.value(), {{{1, 1}, {99, 1}, {99, k - 99}, {k - 49, 49}, {1, 49}}});
}

TEST(Offset, LShapeInflateAndDeflate) {
    // Inflate by 5: outer walls move out, the notch walls move into the notch.
    const auto grown = offsetPolygon(lShape(), 5.0);
    ASSERT_TRUE(grown.ok());
    expectPolygonNear(grown.value(), {{{-5, -5}, {105, -5}, {105, 45}, {45, 45}, {45, 105}, {-5, 105}}});
    EXPECT_NEAR(signedArea(grown.value()), 110.0 * 110.0 - 60.0 * 60.0, 1e-6); // 8500
    // Deflate by 5: arms become 30 wide.
    const auto shrunk = offsetPolygon(lShape(), -5.0);
    ASSERT_TRUE(shrunk.ok());
    expectPolygonNear(shrunk.value(), {{{5, 5}, {95, 5}, {95, 35}, {35, 35}, {35, 95}, {5, 95}}});
    EXPECT_NEAR(signedArea(shrunk.value()), 90.0 * 90.0 - 60.0 * 60.0, 1e-6); // 4500
    // Clockwise L gives the same result.
    const auto cw = offsetPolygon(reversed(lShape()), -5.0);
    ASSERT_TRUE(cw.ok());
    expectPolygonNear(cw.value(), shrunk.value());
}

TEST(Offset, LShapeDeflateBeyondArmWidthIsAnError) {
    // Arms are 40 wide: deflating by 20 makes the 40 mm end edges vanish.
    const auto r = offsetPolygon(lShape(), -20.0);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, ErrorCode::ConstraintViolation);
    EXPECT_NE(r.error().message.find("vanish"), std::string::npos) << r.error().message;
    EXPECT_FALSE(offsetPolygon(lShape(), -25.0).ok());
    EXPECT_TRUE(offsetPolygon(lShape(), -19.0).ok());
}

TEST(Offset, ReflexCornerBevelWhenDeflating) {
    // Arrow head (CCW): tip (100,50), tail corners (0,0)/(0,100), reflex notch vertex (50,50)
    // with an interior angle of 270 deg -> on the deflate side its miter ratio is 1.414:
    // mitered with the default limit, bevelled with limit 1.2.
    const Polygon2 arrow{{{0, 0}, {100, 50}, {0, 100}, {50, 50}}};
    ASSERT_TRUE(isSimple(arrow));
    const auto mitered = offsetPolygon(arrow, -2.0);
    ASSERT_TRUE(mitered.ok());
    ASSERT_EQ(mitered.value().size(), 4u);
    // The notch vertex retreats into the material along +X by 2 * sqrt(2): (52.828, 50).
    EXPECT_TRUE(approxEqual(mitered.value()[3], {50 + 2.0 * std::sqrt(2.0), 50}, 1e-9));
    OffsetOptions opt;
    opt.miterLimit = 1.2;
    const auto bevelled = offsetPolygon(arrow, -2.0, opt);
    ASSERT_TRUE(bevelled.ok());
    // The three convex corners are not on the deflate side: they stay mitered -> 5 vertices.
    // Bevel: cut at x = 52 (2 mm from the vertex); alpha = 45 deg, half width 2 tan(22.5 deg)
    // = 2 (sqrt(2) - 1) = 0.828. First the point on the offset of edge (0,100)->(50,50) (upper).
    ASSERT_EQ(bevelled.value().size(), 5u);
    const double h = 2.0 * (std::sqrt(2.0) - 1.0);
    EXPECT_TRUE(approxEqual(bevelled.value()[3], {52, 50 + h}, 1e-9));
    EXPECT_TRUE(approxEqual(bevelled.value()[4], {52, 50 - h}, 1e-9));
    // The bevel keeps the small triangle between the cut and the miter tip: area + h * h.
    EXPECT_NEAR(signedArea(bevelled.value()) - signedArea(mitered.value()), h * h, 1e-9);
    EXPECT_TRUE(polygonContainsPolygon(arrow, bevelled.value()));
}

TEST(Offset, SelfIntersectingResultIsAnErrorNotGarbage) {
    // U-shape with a 40 mm wide slot: inflating by 25 would close the slot.
    const Polygon2 u{{{0, 0}, {100, 0}, {100, 100}, {70, 100}, {70, 30}, {30, 30}, {30, 100}, {0, 100}}};
    ASSERT_TRUE(isSimple(u));
    EXPECT_TRUE(offsetPolygon(u, 19.0).ok());
    EXPECT_FALSE(offsetPolygon(u, 20.0).ok());
    EXPECT_FALSE(offsetPolygon(u, 25.0).ok());
    // C-shape: cavity [20,80]^2 reached through a 10 mm wide mouth at the right side.
    // Inflating by 6 closes the mouth (walls y = 45 -> 51 and y = 55 -> 49 cross) while no
    // single edge vanishes: the raw outline self-intersects -> Unsupported.
    const Polygon2 c{{{0, 0}, {100, 0}, {100, 45}, {80, 45}, {80, 20}, {20, 20},
                      {20, 80}, {80, 80}, {80, 55}, {100, 55}, {100, 100}, {0, 100}}};
    ASSERT_TRUE(isSimple(c));
    const auto closed = offsetPolygon(c, 6.0);
    ASSERT_FALSE(closed.ok());
    EXPECT_EQ(closed.error().code, ErrorCode::Unsupported);
    // Inflating by 4 leaves a 2 mm mouth (walls at y = 49 and y = 51): fine.
    const auto open = offsetPolygon(c, 4.0);
    ASSERT_TRUE(open.ok());
    EXPECT_TRUE(polygonContainsPolygon(open.value(), c));
    EXPECT_EQ(locatePoint(open.value(), {90, 50}), PointLocation::Outside);    // still in the mouth
    EXPECT_EQ(locatePoint(open.value(), {90, 48.5}), PointLocation::Inside);   // wall moved from 45 to 49
    EXPECT_EQ(locatePoint(open.value(), {50, 22}), PointLocation::Inside);     // cavity wall 20 -> 24
    EXPECT_EQ(locatePoint(open.value(), {50, 26}), PointLocation::Outside);
}

TEST(Offset, InvalidInput) {
    const auto bow = offsetPolygon(Polygon2{{{0, 0}, {10, 10}, {10, 0}, {0, 10}}}, 1.0);
    ASSERT_FALSE(bow.ok());
    EXPECT_EQ(bow.error().code, ErrorCode::InvalidArgument);
    EXPECT_FALSE(offsetPolygon(Polygon2{}, 1.0).ok());
    EXPECT_FALSE(offsetPolygon(Polygon2{{{0, 0}, {10, 0}, {20, 0}}}, 1.0).ok());
    EXPECT_FALSE(offsetPolygon(rect100x50(), std::numeric_limits<double>::quiet_NaN()).ok());
}
