// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/app/plan_view.hpp"
#include "test_support.hpp"

#include "fos/furniture/panel.hpp"
#include <limits>

namespace fos::app::test {
namespace {

project::Item panelItem(const std::string& id, geometry::Transform placement,
                        furniture::PanelOrientation orientation = furniture::PanelOrientation::Horizontal) {
    auto panel = furniture::makePanel(domain::PartId(id + "/panel"), "Panel " + id,
                                      domain::MaterialId(project::defaults::kParticleboard18));
    panel.orientation = orientation;
    return project::Item{project::PanelItem{domain::AssemblyId(id), placement, std::move(panel)}};
}

/// Two horizontal 600 x 300 panels: p1 at the origin, p2 moved 800 mm along X.
struct TwoPanels {
    Session s;
    TwoPanels() {
        s.newProject();
        EXPECT_TRUE(s.execute(project::commands::addItem(panelItem("p1", {}))).ok());
        EXPECT_TRUE(
            s.execute(project::commands::addItem(panelItem("p2", geometry::Transform::translation({800, 0, 0})))).ok());
    }
};

PlanFootprint box(const std::string& id, double x0, double y0, double x1, double y1) {
    PlanFootprint f;
    f.itemId = id;
    f.bounds = {{x0, y0}, {x1, y1}};
    f.outlines.push_back(geometry::makeRectPolygon(f.bounds));
    return f;
}

TEST(PlanView, FootprintsOfPanels) {
    TwoPanels t;
    const auto fp = planFootprints(t.s);
    ASSERT_EQ(fp.size(), 2u);
    EXPECT_EQ(fp[0].itemId, "p1");
    EXPECT_EQ(fp[0].name, "Panel p1");
    // makePanel = 600 x 300 at the origin, lying flat: XY [0,600] x [0,300].
    EXPECT_NEAR(fp[0].bounds.min.x, 0, 1e-9);
    EXPECT_NEAR(fp[0].bounds.max.x, 600, 1e-9);
    EXPECT_NEAR(fp[0].bounds.max.y, 300, 1e-9);
    ASSERT_EQ(fp[0].outlines.size(), 1u);
    EXPECT_NEAR(geometry::area(fp[0].outlines[0]), 600.0 * 300.0, 1e-6);
    EXPECT_FALSE(fp[0].shaped);
    // p2: +800 along X -> [800,1400].
    EXPECT_NEAR(fp[1].bounds.min.x, 800, 1e-9);
    EXPECT_NEAR(fp[1].bounds.max.x, 1400, 1e-9);
    // Union of both: [0,1400] x [0,300].
    const auto ext = planExtents(fp);
    EXPECT_NEAR(ext.width(), 1400, 1e-9);
    EXPECT_NEAR(ext.height(), 300, 1e-9);
    EXPECT_TRUE(planExtents({}).empty());
}

TEST(PlanView, StandingPanelProjectsToItsBox) {
    Session s;
    s.newProject();
    ASSERT_TRUE(s.execute(project::commands::addItem(panelItem("v", {}, furniture::PanelOrientation::VerticalSide))).ok());
    const auto fp = planFootprints(s);
    ASSERT_EQ(fp.size(), 1u);
    ASSERT_EQ(fp[0].outlines.size(), 1u);
    // A standing part contributes the XY rectangle of its box: same as the item bounds.
    EXPECT_EQ(geometry::bounds(fp[0].outlines[0]), fp[0].bounds);
    // Thickness 18 along X, width 300 along Y.
    EXPECT_NEAR(fp[0].bounds.width(), 18, 1e-9);
    EXPECT_NEAR(fp[0].bounds.height(), 300, 1e-9);
}

TEST(PlanView, GapsBetweenNeighboursAndWalls) {
    TwoPanels t;
    const auto fp = planFootprints(t.s);
    auto gaps = planGapsX(fp, std::nullopt);
    ASSERT_EQ(gaps.size(), 1u);
    EXPECT_EQ(gaps[0].leftId, "p1");
    EXPECT_EQ(gaps[0].rightId, "p2");
    EXPECT_NEAR(gaps[0].gap(), 200, 1e-9); // 800 - 600
    EXPECT_NEAR(gaps[0].y, 150, 1e-9);     // middle of the shared Y range [0,300]

    // Room inner [-100,1500] x [0,1000]: wall gaps 0 - (-100) = 100 and 1500 - 1400 = 100.
    const project::RoomSettings room{1600, 1000, std::nullopt, 120, {-100, 0}};
    gaps = planGapsX(fp, room);
    ASSERT_EQ(gaps.size(), 3u);
    EXPECT_TRUE(gaps[0].leftId.empty());
    EXPECT_EQ(gaps[0].rightId, "p1");
    EXPECT_NEAR(gaps[0].gap(), 100, 1e-9);
    EXPECT_EQ(gaps[1].leftId, "p1");
    EXPECT_EQ(gaps[2].leftId, "p2");
    EXPECT_TRUE(gaps[2].rightId.empty());
    EXPECT_NEAR(gaps[2].gap(), 100, 1e-9);
    // An invalid room is ignored.
    EXPECT_EQ(planGapsX(fp, project::RoomSettings{-5, 1000, std::nullopt, 100, {0, 0}}).size(), 1u);
}

TEST(PlanView, GapsUseNearestNeighbourWithSharedDepthOnly) {
    // a [0,100]x[0,100], b [150,250]x[0,100], c [120,140]x[500,600] (no shared Y with a),
    // d [300,400]x[50,80] (behind b along X). Gaps: a->b 50, b->d 50; c has no neighbour.
    // e [100,200]x[200,300] touches nothing; f [200.03,300]x[200,300] touches e (0.03 <= 0.05).
    const std::vector<PlanFootprint> fp{box("a", 0, 0, 100, 100),     box("b", 150, 0, 250, 100),
                                        box("c", 120, 500, 140, 600), box("d", 300, 50, 400, 80),
                                        box("e", 100, 200, 200, 300), box("f", 200.03, 200, 300, 300)};
    const auto gaps = planGapsX(fp, std::nullopt);
    ASSERT_EQ(gaps.size(), 2u);
    EXPECT_EQ(gaps[0].leftId, "a");
    EXPECT_EQ(gaps[0].rightId, "b");
    EXPECT_NEAR(gaps[0].gap(), 50, 1e-9);
    EXPECT_EQ(gaps[1].leftId, "b");
    EXPECT_EQ(gaps[1].rightId, "d");
    EXPECT_NEAR(gaps[1].y, 65, 1e-9); // middle of [50,80]
}

TEST(PlanView, SnapToEdgesAndWalls) {
    const std::vector<PlanFootprint> fp{box("p1", 0, 0, 600, 300), box("p2", 800, 0, 1400, 300)};
    // p2 dragged by (-195, 3.4): its left edge 605 is 5 mm from p1's right edge 600 -> dx = -200;
    // its front edge 3.4 is 3.4 mm from y = 0 -> dy = 0.
    auto snap = snapPlanMove(fp, "p2", {-195, 3.4}, 10, std::nullopt);
    EXPECT_DOUBLE_EQ(snap.delta.x, -200);
    EXPECT_DOUBLE_EQ(snap.delta.y, 0);
    ASSERT_TRUE(snap.guideX && snap.guideY);
    EXPECT_DOUBLE_EQ(*snap.guideX, 600);
    EXPECT_DOUBLE_EQ(*snap.guideY, 0);

    // Out of tolerance: rounded to the 1 mm grid, no guides.
    snap = snapPlanMove(fp, "p2", {-150.4, 50.6}, 10, std::nullopt);
    EXPECT_DOUBLE_EQ(snap.delta.x, -150);
    EXPECT_DOUBLE_EQ(snap.delta.y, 51);
    EXPECT_FALSE(snap.guideX || snap.guideY);

    // Room inner [0,1500] x [0,1000]: p2's right edge 1400 + 96 = 1496 snaps to the wall 1500.
    snap = snapPlanMove(fp, "p2", {96, 300.2}, 10, project::RoomSettings{1500, 1000, std::nullopt, 100, {0, 0}});
    EXPECT_DOUBLE_EQ(snap.delta.x, 100);
    EXPECT_DOUBLE_EQ(*snap.guideX, 1500);
    // Y: front edge 300.2 snaps to p1's back edge 300 (0.2) -> dy = 300.
    EXPECT_DOUBLE_EQ(snap.delta.y, 300);

    // Unknown item: unchanged apart from rounding.
    snap = snapPlanMove(fp, "nope", {1.6, 2.2}, 10, std::nullopt);
    EXPECT_DOUBLE_EQ(snap.delta.x, 2);
    EXPECT_DOUBLE_EQ(snap.delta.y, 2);
}

TEST(PlanView, MoveIsOneUndoableSetPlacement) {
    TwoPanels t;
    const auto rev = t.s.revision();
    ASSERT_TRUE(applyPlanMove(t.s, "p2", {-200, 0}).ok());
    EXPECT_NEAR(t.s.project().findItem("p2")->placement().origin.x, 600, 1e-9);
    EXPECT_TRUE(planGapsX(planFootprints(t.s), std::nullopt).empty()); // touching now
    ASSERT_TRUE(t.s.undo().ok());
    EXPECT_NEAR(t.s.project().findItem("p2")->placement().origin.x, 800, 1e-9);
    EXPECT_EQ(t.s.revision(), rev + 2);
    EXPECT_FALSE(applyPlanMove(t.s, "missing", {1, 0}).ok());
    EXPECT_FALSE(applyPlanMove(t.s, "p1", {std::numeric_limits<double>::quiet_NaN(), 0}).ok());
}

TEST(PlanView, RotateAboutFootprintCentre) {
    TwoPanels t;
    ASSERT_TRUE(applyPlanRotate(t.s, "p1").ok());
    // p1 [0,600] x [0,300], centre (300,150); rotated 90 degrees: 300 along X, 600 along Y
    // around the same centre -> [150,450] x [-150,450].
    auto fp = planFootprints(t.s);
    EXPECT_NEAR(fp[0].bounds.min.x, 150, 1e-9);
    EXPECT_NEAR(fp[0].bounds.max.x, 450, 1e-9);
    EXPECT_NEAR(fp[0].bounds.min.y, -150, 1e-9);
    EXPECT_NEAR(fp[0].bounds.max.y, 450, 1e-9);
    EXPECT_TRUE(t.s.project().findItem("p1")->placement().isRigid());
    // Four rotations return to the start exactly (exact axes).
    for (int i = 0; i < 3; ++i) ASSERT_TRUE(applyPlanRotate(t.s, "p1").ok());
    EXPECT_TRUE(geometry::approxEqual(t.s.project().findItem("p1")->placement(), geometry::Transform{}));
    for (int i = 0; i < 4; ++i) ASSERT_TRUE(t.s.undo().ok());
    fp = planFootprints(t.s);
    EXPECT_NEAR(fp[0].bounds.max.x, 600, 1e-9);
    EXPECT_FALSE(applyPlanRotate(t.s, "missing").ok());
}

TEST(PlanView, HitTest) {
    TwoPanels t;
    const auto fp = planFootprints(t.s);
    EXPECT_EQ(planItemAt(fp, {100, 100}), "p1");
    EXPECT_EQ(planItemAt(fp, {1000, 299}), "p2");
    EXPECT_EQ(planItemAt(fp, {700, 100}), "");
    EXPECT_EQ(planItemAt(fp, {603, 100}, 5), "p1"); // within 5 mm of p1's bounds
}

TEST(PlanView, RoomRoundTripThroughCommandAndUndo) {
    TwoPanels t;
    EXPECT_FALSE(planRoom(t.s).has_value());
    const project::RoomSettings room{3000, 2000, 2500.0, 100, {-50, 10}};
    // Inner rectangle [-50, 2950] x [10, 2010].
    const auto inner = planRoomInner(room);
    EXPECT_DOUBLE_EQ(inner.min.x, -50);
    EXPECT_DOUBLE_EQ(inner.max.x, 2950);
    EXPECT_DOUBLE_EQ(inner.min.y, 10);
    EXPECT_DOUBLE_EQ(inner.max.y, 2010);

    ASSERT_TRUE(applyPlanRoom(t.s, room).ok());
    ASSERT_TRUE(planRoom(t.s).has_value());
    EXPECT_EQ(*planRoom(t.s), room);
    EXPECT_EQ(t.s.project().settings.room, room);

    // Same room again: no new undo step (one undo below goes straight back to no room).
    ASSERT_TRUE(applyPlanRoom(t.s, room).ok());
    project::RoomSettings edited = room;
    edited.height.reset(); // 0 in the GUI = not set
    edited.width = 3200;
    ASSERT_TRUE(applyPlanRoom(t.s, edited).ok());
    EXPECT_EQ(*planRoom(t.s), edited);

    // Invalid rooms are refused and change nothing.
    project::RoomSettings bad = edited;
    bad.depth = 0;
    EXPECT_FALSE(applyPlanRoom(t.s, bad).ok());
    bad = edited;
    bad.height = -1.0;
    EXPECT_FALSE(applyPlanRoom(t.s, bad).ok());
    bad = edited;
    bad.origin.x = std::numeric_limits<double>::infinity();
    EXPECT_FALSE(applyPlanRoom(t.s, bad).ok());
    EXPECT_EQ(*planRoom(t.s), edited);

    // Remove, then undo step by step: removed -> edited -> room -> none; redo brings it back.
    ASSERT_TRUE(applyPlanRoom(t.s, std::nullopt).ok());
    EXPECT_FALSE(planRoom(t.s).has_value());
    ASSERT_TRUE(t.s.undo().ok());
    EXPECT_EQ(*planRoom(t.s), edited);
    ASSERT_TRUE(t.s.undo().ok());
    EXPECT_EQ(*planRoom(t.s), room);
    ASSERT_TRUE(t.s.undo().ok());
    EXPECT_FALSE(planRoom(t.s).has_value());
    EXPECT_EQ(planFootprints(t.s).size(), 2u); // the items were not touched
    ASSERT_TRUE(t.s.redo().ok());
    EXPECT_EQ(*planRoom(t.s), room);
    // Removing when there is no room is a no-op that succeeds.
    ASSERT_TRUE(t.s.undo().ok());
    EXPECT_TRUE(applyPlanRoom(t.s, std::nullopt).ok());

    // The project room drives snapping: p2 [800,1400] right edge + 46 = 1446 is 4 mm from the
    // inner wall 1450 of a room at origin 0 -> dx = 50.
    const project::RoomSettings small{1450, 1000, std::nullopt, 100, {0, 0}};
    ASSERT_TRUE(applyPlanRoom(t.s, small).ok());
    const auto snap = snapPlanMove(planFootprints(t.s), "p2", {46, 0}, 10, planRoom(t.s));
    EXPECT_DOUBLE_EQ(snap.delta.x, 50);
    ASSERT_TRUE(snap.guideX.has_value());
    EXPECT_DOUBLE_EQ(*snap.guideX, 1450);
}

TEST(PlanView, Texts) {
    EXPECT_EQ(planSizeText(600, 300.04), QStringLiteral("600 × 300 mm"));
    EXPECT_EQ(planLengthText(12.25), QStringLiteral("12.3 mm")); // 122.5 rounds away from zero
    EXPECT_EQ(planLengthText(200), QStringLiteral("200 mm"));
}

} // namespace
} // namespace fos::app::test
