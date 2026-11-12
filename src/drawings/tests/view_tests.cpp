// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/drawings/hlr.hpp"
#include "test_util.hpp"

using namespace fos;
using namespace fos::drawings;
using namespace fos::drawings::testing;

// Fixture boxes in the assembly frame (from fixtures.hpp, worked out by hand):
//   side.left   x [0,18]     y [0,560]    z [0,720]
//   side.right  x [782,800]  y [0,560]    z [0,720]
//   bottom      x [18,782]   y [0,560]    z [0,18]
//   top         x [18,782]   y [0,560]    z [702,720]
//   shelf.1     x [18,782]   y [20,560]   z [351,369]
//   back        x [2,798]    y [560,563]  z [2,718]

TEST(ViewBasis, TableIsRightHandedAndMatchesDocumentation) {
    const Vec3 X{1, 0, 0}, Y{0, 1, 0}, Z{0, 0, 1};
    struct Row {
        ViewKind kind;
        Vec3 right, up, toViewer;
    };
    const Row rows[] = {{ViewKind::Front, X, Z, -Y}, {ViewKind::Back, -X, Z, Y}, {ViewKind::Left, -Y, Z, -X},
                        {ViewKind::Right, Y, Z, X},  {ViewKind::Top, X, Y, Z},   {ViewKind::Bottom, X, -Y, -Z}};
    for (const Row& r : rows) {
        const ViewBasis b = viewBasis(r.kind);
        EXPECT_EQ(b.right, r.right) << toString(r.kind);
        EXPECT_EQ(b.up, r.up) << toString(r.kind);
        EXPECT_EQ(b.toViewer, r.toViewer) << toString(r.kind);
        EXPECT_EQ(cross(b.right, b.up), b.toViewer) << toString(r.kind) << " must be right-handed";
    }
}

TEST(ViewBasis, ProjectsAPointAsDocumented) {
    const Vec3 p{10, 20, 30};
    // (u, v, depth) per the table in view.hpp.
    const struct {
        ViewKind kind;
        double u, v, depth;
    } rows[] = {{ViewKind::Front, 10, 30, -20}, {ViewKind::Back, -10, 30, 20}, {ViewKind::Left, -20, 30, -10},
                {ViewKind::Right, 20, 30, 10},  {ViewKind::Top, 10, 20, 30},   {ViewKind::Bottom, 10, -20, -30}};
    for (const auto& r : rows) {
        const ViewBasis b = viewBasis(r.kind);
        EXPECT_EQ(projectPoint(b, p), (Vec2{r.u, r.v})) << toString(r.kind);
        EXPECT_EQ(depthOf(b, p), r.depth) << toString(r.kind);
    }
    EXPECT_STREQ(toString(ViewKind::Bottom), "bottom");
}

TEST(Geom2d, ClipSegmentToRect) {
    const Rect2 r{{0, 0}, {10, 10}};
    const auto c = clipSegmentToRect({{-5, 5}, {15, 5}}, r);
    ASSERT_TRUE(c);
    EXPECT_TRUE(samePoint(c->a, {0, 5}) && samePoint(c->b, {10, 5}));
    // Diagonal from (-5,0) to (5,10): enters at (0,5), leaves at (5,10).
    const auto d = clipSegmentToRect({{-5, 0}, {5, 10}}, r);
    ASSERT_TRUE(d);
    EXPECT_TRUE(samePoint(d->a, {0, 5}) && samePoint(d->b, {5, 10}));
    EXPECT_FALSE(clipSegmentToRect({{11, 0}, {11, 10}}, r));
    EXPECT_FALSE(clipSegmentToRect({{-5, 14}, {5, 16}}, r));
    // A segment only touching a corner has no length inside.
    EXPECT_FALSE(clipSegmentToRect({{-5, 5}, {5, -5}}, r));
    // Along the boundary: inside the closed rectangle but not crossing its interior.
    EXPECT_TRUE(clipSegmentToRect({{0, 2}, {0, 8}}, r));
    EXPECT_FALSE(segmentCrossesRect({{0, 2}, {0, 8}}, r));
    EXPECT_TRUE(segmentCrossesRect({{1, 2}, {1, 8}}, r));
    EXPECT_TRUE(intersects(r, Rect2{{9, 9}, {12, 12}}));
    EXPECT_FALSE(intersects(r, Rect2{{10, 0}, {12, 10}})) << "touching rectangles do not intersect";
}

TEST(Geom2d, HatchAt45DegreesIsClippedToTheRectangle) {
    // Lines y = x + c with c = k * spacing * sqrt(2). With spacing = sqrt(2): c = 2k.
    // Inside the 10 x 10 square |c| < 10, so k = -4 .. 4 -> 9 lines (c = +-10 only touch corners).
    const Rect2 r{{0, 0}, {10, 10}};
    const auto h = hatchRect(r, 45.0, std::sqrt(2.0));
    ASSERT_EQ(h.size(), 9u);
    for (const Segment2& s : h) {
        EXPECT_TRUE(r.contains(s.a, 1e-9) && r.contains(s.b, 1e-9));
        EXPECT_NEAR(s.b.x - s.a.x, s.b.y - s.a.y, 1e-9) << "45 degree direction";
    }
    // k = -4 (first): y = x - 8 -> (8,0)-(10,2); k = 0: the diagonal; k = 2: y = x + 4 -> (0,4)-(6,10).
    EXPECT_TRUE(samePoint(h[0].a, {8, 0}, 1e-9) && samePoint(h[0].b, {10, 2}, 1e-9));
    EXPECT_TRUE(samePoint(h[4].a, {0, 0}, 1e-9) && samePoint(h[4].b, {10, 10}, 1e-9));
    EXPECT_TRUE(samePoint(h[6].a, {0, 4}, 1e-9) && samePoint(h[6].b, {6, 10}, 1e-9));
    EXPECT_TRUE(hatchRect(r, 45.0, 0.0).empty());
    EXPECT_TRUE(hatchRect(Rect2{}, 45.0, 1.0).empty());

    // Shelf cross-section of the fixture in the Left section: u [-560,-20], v [351,369].
    // n = (-sin45, cos45); n.p ranges from (20+351)/sqrt2 = 262.34 to (560+369)/sqrt2 = 656.90.
    // spacing 20 -> k = ceil(13.117) = 14 .. floor(32.845) = 32 -> 19 lines.
    const Rect2 shelf{{-560, 351}, {-20, 369}};
    const auto hs = hatchRect(shelf, 45.0, 20.0);
    EXPECT_EQ(hs.size(), 19u);
    for (const Segment2& s : hs) EXPECT_TRUE(shelf.contains(s.a, 1e-9) && shelf.contains(s.b, 1e-9));
    // 135 degrees: direction (-1, 1).
    for (const Segment2& s : hatchRect(shelf, 135.0, 20.0)) EXPECT_NEAR(s.b.x - s.a.x, -(s.b.y - s.a.y), 1e-9);
}

TEST(Hlr, EdgeBehindANearerBoxIsSplitIntoVisibleAndHiddenParts) {
    // A: 10 x 10 square, far away. B: nearer, covers x [5,15], y [2,8].
    const std::vector<HlrBox> boxes = {{{{0, 0}, {10, 10}}, 0, -5, "A"}, {{{5, 2}, {15, 8}}, 10, 5, "B"}};
    View v;
    v.segments = removeHiddenLines(boxes);
    // A's right edge x = 10: visible 0..2, hidden 2..8, visible 8..10.
    const ViewSegment* s = findSegment(v, {10, 0}, {10, 2});
    ASSERT_TRUE(s);
    EXPECT_EQ(s->style, LineStyle::Visible);
    s = findSegment(v, {10, 2}, {10, 8});
    ASSERT_TRUE(s);
    EXPECT_EQ(s->style, LineStyle::Hidden);
    EXPECT_EQ(s->sourceId, "A");
    ASSERT_TRUE(findSegment(v, {10, 8}, {10, 10}));
    // A's other edges are untouched, B is fully visible.
    ASSERT_TRUE(findSegment(v, {0, 10}, {10, 10}));
    ASSERT_TRUE(findSegment(v, {5, 2}, {5, 8}));
    EXPECT_EQ(findSegment(v, {5, 2}, {5, 8})->style, LineStyle::Visible);
    // 3 whole edges of A + 3 pieces + 4 edges of B = 10 (near and far faces coincide and merge).
    EXPECT_EQ(v.segments.size(), 10u);
    EXPECT_EQ(countStyle(v, LineStyle::Hidden), 1u);
    // Omitting hidden lines removes exactly that piece.
    EXPECT_EQ(removeHiddenLines(boxes, {}, HiddenLineMode::Omit).size(), 9u);
    // A box at the same depth does not hide (tolerance): move B back to A's near depth.
    const std::vector<HlrBox> flush = {boxes[0], {{{5, 2}, {15, 8}}, 0, -5, "B"}};
    for (const ViewSegment& seg : removeHiddenLines(flush)) EXPECT_EQ(seg.style, LineStyle::Visible);
}

TEST(Hlr, FrontViewOfTheCabinet) {
    const domain::Model m = domain::fixtures::basicCabinet();
    const View v = generateAssemblyView(cabinet(m), ViewKind::Front);
    EXPECT_EQ(v.id, "cab1/view.front");
    EXPECT_TRUE(v.diagnostics.empty());
    EXPECT_EQ(v.bounds.min, (Vec2{0, 0}));
    EXPECT_EQ(v.bounds.max, (Vec2{800, 720}));

    // The shelf's front edges: visible between x = 18 and x = 782 at z = 351 and z = 369.
    for (const double z : {351.0, 369.0}) {
        const ViewSegment* s = findSegment(v, {18, z}, {782, z});
        ASSERT_TRUE(s) << "z = " << z;
        EXPECT_EQ(s->style, LineStyle::Visible);
        EXPECT_EQ(s->sourceId, "cab1/shelf.1");
    }
    // The back panel (x [2,798], z [2,718]) lies behind the sides, bottom and top:
    // its whole outline is hidden, each side as ONE merged segment.
    const Vec2 backCorners[4] = {{2, 2}, {798, 2}, {798, 718}, {2, 718}};
    for (int i = 0; i < 4; ++i) {
        const ViewSegment* s = findSegment(v, backCorners[i], backCorners[(i + 1) % 4]);
        ASSERT_TRUE(s) << "back edge " << i;
        EXPECT_EQ(s->style, LineStyle::Hidden);
        EXPECT_EQ(s->sourceId, "cab1/back");
    }
    for (const ViewSegment& s : v.segments)
        if (s.sourceId == "cab1/back") { EXPECT_EQ(s.style, LineStyle::Hidden); }

    // Carcass outline: inner edge of the left side is one visible line owned by the side
    // (the coincident ends of bottom, shelf and top are merged into it).
    const ViewSegment* inner = findSegment(v, {18, 0}, {18, 720});
    ASSERT_TRUE(inner);
    EXPECT_EQ(inner->style, LineStyle::Visible);
    EXPECT_EQ(inner->sourceId, "cab1/side.left");
    // Floor line is split by owner: side 0..18, bottom 18..782, side 782..800.
    ASSERT_TRUE(findSegment(v, {0, 0}, {18, 0}));
    ASSERT_TRUE(findSegment(v, {18, 0}, {782, 0}));
    EXPECT_EQ(findSegment(v, {18, 0}, {782, 0})->sourceId, "cab1/bottom");
    ASSERT_TRUE(findSegment(v, {782, 0}, {800, 0}));

    // Complete inventory (by hand): verticals at x = 0, 18, 782, 800 (4 visible) and x = 2, 798
    // (2 hidden); horizontals z = 0 and 720 (3 pieces each), z = 18, 351, 369, 702 (1 each),
    // z = 2 and 718 (hidden). Total 4 + 2 + 6 + 4 + 2 = 18, of which 4 hidden.
    EXPECT_EQ(v.segments.size(), 18u);
    EXPECT_EQ(countStyle(v, LineStyle::Hidden), 4u);

    ViewOptions omit;
    omit.hiddenLines = HiddenLineMode::Omit;
    const View o = generateAssemblyView(cabinet(m), ViewKind::Front, omit);
    EXPECT_EQ(o.segments.size(), 14u);
    EXPECT_EQ(countStyle(o, LineStyle::Hidden), 0u);
    EXPECT_EQ(o.bounds.max, (Vec2{800, 720})) << "bounds do not depend on hidden-line mode";
}

TEST(Hlr, TopViewHidesTheShelfUnderTheTopPanel) {
    const domain::Model m = domain::fixtures::basicCabinet();
    const View v = generateAssemblyView(cabinet(m), ViewKind::Top);
    EXPECT_EQ(v.bounds.max, (Vec2{800, 563}));
    // Shelf front edge at y = 20 lies under the top panel (y 0..560, z up to 720).
    const ViewSegment* s = findSegment(v, {18, 20}, {782, 20});
    ASSERT_TRUE(s);
    EXPECT_EQ(s->style, LineStyle::Hidden);
    EXPECT_EQ(s->sourceId, "cab1/shelf.1");
    // No part of the shelf is visible from above.
    for (const ViewSegment& seg : v.segments)
        if (seg.sourceId == "cab1/shelf.1") { EXPECT_EQ(seg.style, LineStyle::Hidden); }
    // The back panel sticks out behind the carcass: its rear edge y = 563 is visible.
    const ViewSegment* rear = findSegment(v, {2, 563}, {798, 563});
    ASSERT_TRUE(rear);
    EXPECT_EQ(rear->style, LineStyle::Visible);
    EXPECT_EQ(rear->sourceId, "cab1/back");
    // Front edge of the top panel.
    ASSERT_TRUE(findSegment(v, {18, 0}, {782, 0}));
    EXPECT_EQ(findSegment(v, {18, 0}, {782, 0})->sourceId, "cab1/top");
}

TEST(Hlr, LeftAndBackViewsUseTheirOwnAxes) {
    const domain::Model m = domain::fixtures::basicCabinet();
    // Left view: u = -y, so the cabinet spans u [-563, 0]; only the left side is visible, the
    // shelf (behind it, y 20..560 -> u -560..-20) is hidden.
    const View l = generateAssemblyView(cabinet(m), ViewKind::Left);
    EXPECT_EQ(l.bounds.min, (Vec2{-563, 0}));
    EXPECT_EQ(l.bounds.max, (Vec2{0, 720}));
    const ViewSegment* s = findSegment(l, {-560, 351}, {-20, 351});
    ASSERT_TRUE(s);
    EXPECT_EQ(s->style, LineStyle::Hidden);
    // Back view: u = -x; the back panel is nearest and fully visible.
    const View b = generateAssemblyView(cabinet(m), ViewKind::Back);
    EXPECT_EQ(b.bounds.min, (Vec2{-800, 0}));
    const ViewSegment* edge = findSegment(b, {-798, 2}, {-2, 2});
    ASSERT_TRUE(edge);
    EXPECT_EQ(edge->style, LineStyle::Visible);
    EXPECT_EQ(edge->sourceId, "cab1/back");
    // ... and it hides the shelf completely.
    for (const ViewSegment& seg : b.segments)
        if (seg.sourceId == "cab1/shelf.1") { EXPECT_EQ(seg.style, LineStyle::Hidden); }
}

TEST(Section, VerticalPlaneThroughTheMiddleOfTheCabinet) {
    const domain::Model m = domain::fixtures::basicCabinet();
    ViewOptions o;
    o.sectionPosition = 400.0; // plane x = 400, looking from the left (+X direction)
    const View v = generateAssemblyView(cabinet(m), ViewKind::Left, o);
    EXPECT_EQ(v.id, "cab1/section.left");
    // The left side (x 0..18) is on the viewer's side and removed; the right side is behind.
    ASSERT_EQ(v.footprints.size(), 5u);
    EXPECT_EQ(v.footprints[0].partId, "cab1/side.right");
    EXPECT_FALSE(v.footprints[0].cut);
    // Cut parts in assembly order with alternating hatch angle; rect = (u = -y, v = z).
    ASSERT_EQ(v.cuts.size(), 4u);
    const struct {
        const char* id;
        Rect2 rect;
        double angle;
    } expected[] = {{"cab1/bottom", {{-560, 0}, {0, 18}}, 45},
                    {"cab1/top", {{-560, 702}, {0, 720}}, 135},
                    {"cab1/shelf.1", {{-560, 351}, {-20, 369}}, 45},
                    {"cab1/back", {{-563, 2}, {-560, 718}}, 135}};
    for (std::size_t i = 0; i < 4; ++i) {
        EXPECT_EQ(v.cuts[i].sourceId, expected[i].id);
        EXPECT_EQ(v.cuts[i].rect.min, expected[i].rect.min) << expected[i].id;
        EXPECT_EQ(v.cuts[i].rect.max, expected[i].rect.max) << expected[i].id;
        EXPECT_EQ(v.cuts[i].hatchAngleDeg, expected[i].angle);
    }
    // Cut faces lie in the plane: depth = -400 (toViewer = -X).
    EXPECT_EQ(v.footprints[1].depthNear, -400.0);
    EXPECT_TRUE(v.footprints[1].cut);
    // Outline of the shelf cross-section is visible.
    const ViewSegment* s = findSegment(v, {-560, 351}, {-20, 351});
    ASSERT_TRUE(s);
    EXPECT_EQ(s->style, LineStyle::Visible);
    EXPECT_EQ(s->sourceId, "cab1/shelf.1");
    ASSERT_TRUE(findSegment(v, {-20, 351}, {-20, 369}));
    // Behind the plane the right side shows its front edge (u = 0) between bottom and top.
    const ViewSegment* front = findSegment(v, {0, 18}, {0, 702});
    ASSERT_TRUE(front);
    EXPECT_EQ(front->style, LineStyle::Visible);
    EXPECT_EQ(front->sourceId, "cab1/side.right");
    EXPECT_EQ(countStyle(v, LineStyle::Hidden), 0u) << "nothing is hidden behind this cut";
    EXPECT_EQ(v.bounds.min, (Vec2{-563, 0}));

    // Rendering emits hatch lines (Thin) inside each cut rectangle. Paper spacing 2 mm at 1:10 =
    // 20 mm in the model -> 19 lines in the shelf (see Geom2d test).
    DrawingStyle style;
    const auto prims = renderView(v, {}, ViewPlacement{{100, 50}, 0.1}, style);
    std::size_t shelfHatch = 0;
    for (const Primitive& p : prims) {
        const Line* l = std::get_if<Line>(&p);
        if (!l || l->style != LineStyle::Thin || l->sourceId != "cab1/shelf.1") continue;
        ++shelfHatch;
        const Rect2 paper{{100 - 56, 50 + 35.1}, {100 - 2, 50 + 36.9}};
        EXPECT_TRUE(paper.contains(l->a, 1e-9) && paper.contains(l->b, 1e-9));
    }
    EXPECT_EQ(shelfHatch, 19u);
}

TEST(Section, HorizontalPlaneAndPlaneOutsideTheModel) {
    const domain::Model m = domain::fixtures::basicCabinet();
    ViewOptions o;
    o.sectionPosition = 360.0; // plane z = 360 seen from above: cuts sides, shelf and back
    const View v = generateAssemblyView(cabinet(m), ViewKind::Top, o);
    ASSERT_EQ(v.cuts.size(), 4u);
    EXPECT_EQ(v.cuts[0].sourceId, "cab1/side.left");
    EXPECT_EQ(v.cuts[1].sourceId, "cab1/side.right");
    EXPECT_EQ(v.cuts[2].sourceId, "cab1/shelf.1");
    EXPECT_EQ(v.cuts[3].sourceId, "cab1/back");
    // The top panel (z 702..720) is above the plane and removed; the bottom is behind, uncut.
    ASSERT_EQ(v.footprints.size(), 5u);
    for (const PartFootprint& f : v.footprints) EXPECT_NE(f.partId, "cab1/top");
    // A part whose face lies exactly in the plane is behind it, not cut: z = 369 is the shelf top.
    o.sectionPosition = 369.0;
    const View flush = generateAssemblyView(cabinet(m), ViewKind::Top, o);
    for (const CutRegion& c : flush.cuts) EXPECT_NE(c.sourceId, "cab1/shelf.1");
    // Plane below everything: nothing remains.
    o.sectionPosition = -10.0;
    const View empty = generateAssemblyView(cabinet(m), ViewKind::Top, o);
    EXPECT_TRUE(empty.segments.empty());
    EXPECT_TRUE(empty.bounds.empty());
}

TEST(Hlr, RotatedPartFallsBackToAllEdgesVisibleAndIsReported) {
    domain::Model m = domain::fixtures::basicCabinet();
    domain::Part door = m.assemblies[0].parts[4]; // copy of the shelf
    door.id = domain::PartId("cab1/door");
    door.name = "Open door";
    door.length = 700;
    door.width = 400;
    door.thickness = 18;
    // Standing panel swung 30 degrees about Z in front of the cabinet.
    door.placement = geometry::Transform::translation({0, -500, 0}) * geometry::Transform::rotationZ(30) *
                     geometry::Transform::fromAxes({0, 0, 1}, {1, 0, 0}, {0, 0, 0});
    ASSERT_FALSE(isAxisAligned(door.placement));
    ASSERT_TRUE(isAxisAligned(m.assemblies[0].parts[0].placement));
    m.assemblies[0].parts.push_back(door);
    const View v = generateAssemblyView(m.assemblies[0], ViewKind::Front);
    ASSERT_EQ(v.diagnostics.size(), 1u);
    EXPECT_EQ(v.diagnostics[0].code, "drawings.hlr.not_axis_aligned");
    EXPECT_EQ(v.diagnostics[0].entityId, "cab1/door");
    EXPECT_NE(v.diagnostics[0].message.find("Open door"), std::string::npos);
    std::size_t doorSegments = 0;
    for (const ViewSegment& s : v.segments)
        if (s.sourceId == "cab1/door") {
            ++doorSegments;
            EXPECT_EQ(s.style, LineStyle::Visible);
        }
    EXPECT_GT(doorSegments, 0u);
    // It hides nothing: the cabinet behind it is drawn exactly as without the door.
    EXPECT_EQ(countStyle(v, LineStyle::Hidden), 4u);
    ASSERT_TRUE(findSegment(v, {18, 351}, {782, 351}));
    EXPECT_EQ(v.footprints.back().aspect, PanelAspect::Oblique);
}

TEST(View, DegeneratePartIsSkippedWithDiagnostic) {
    domain::Model m = domain::fixtures::basicCabinet();
    m.assemblies[0].parts[4].thickness = 0;
    const View v = generateAssemblyView(m.assemblies[0], ViewKind::Front);
    ASSERT_EQ(v.diagnostics.size(), 1u);
    EXPECT_EQ(v.diagnostics[0].code, "drawings.view.degenerate_part");
    EXPECT_EQ(v.footprints.size(), 5u);
    EXPECT_FALSE(findSegment(v, {18, 351}, {782, 351}));
}
