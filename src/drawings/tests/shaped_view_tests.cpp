// SPDX-License-Identifier: AGPL-3.0-only
// Contoured parts in assembly views (phase 12, ADR-0008).
#include "fos/drawings/hlr.hpp"
#include "shaped_fixtures.hpp"

using namespace fos;
using namespace fos::drawings;
using namespace fos::drawings::testing;

namespace {

domain::Part box(const std::string& id, double L, double W, double T, Vec3 at) {
    domain::Part p;
    p.id = domain::PartId("cab1/" + id);
    p.name = id;
    p.length = L;
    p.width = W;
    p.thickness = T;
    p.material = domain::MaterialId("mat.pb18");
    p.placement = geometry::Transform::translation(at);
    return p;
}

bool hasCode(const View& v, const std::string& code) {
    for (const auto& d : v.diagnostics)
        if (d.code == code) return true;
    return false;
}

std::size_t countSource(const View& v, const std::string& src, LineStyle style) {
    std::size_t n = 0;
    for (const ViewSegment& s : v.segments) n += (s.sourceId == src && s.style == style) ? 1 : 0;
    return n;
}

} // namespace

TEST(ShapedAssemblyView, FaceOnWorktopDrawsOutlineAndHolesAndWarnsAboutApproximation) {
    domain::Assembly a;
    a.id = domain::AssemblyId("cab1");
    domain::Part top = worktop();
    top.placement = geometry::Transform::translation({0, 0, 100}); // z 100..138, face-on in the Top view
    a.parts.push_back(top);
    // 100 x 100 box below, entirely under the sink opening [350,850] x [100,500]: really visible.
    a.parts.push_back(box("under.sink", 100, 100, 18, {400, 200, 0}));
    const View v = generateAssemblyView(a, ViewKind::Top);
    // Worktop outline (4) and sink (4) are visible lines of the worktop.
    EXPECT_EQ(countSource(v, "cab1/worktop", LineStyle::Visible), 8u);
    EXPECT_TRUE(findSegment(v, {350, 100}, {850, 100}));
    EXPECT_TRUE(findSegment(v, {0, 0}, {1200, 0}));
    // Tap hole: two exact half circles R 17.5 around (600, 540).
    ASSERT_EQ(v.arcs.size(), 2u);
    for (const ViewArc& arc : v.arcs) {
        EXPECT_TRUE(samePoint(arc.center, {600, 540}));
        EXPECT_NEAR(arc.radius, 17.5, 1e-9);
        EXPECT_NEAR(sweepOf(arc), 180, 1e-9);
        EXPECT_EQ(arc.style, LineStyle::Visible);
    }
    // APPROXIMATION: the box under the sink is hidden by the worktop's bounding rectangle
    // (drawn dashed) and the approximation is reported.
    EXPECT_EQ(countSource(v, "cab1/under.sink", LineStyle::Visible), 0u);
    EXPECT_EQ(countSource(v, "cab1/under.sink", LineStyle::Hidden), 4u);
    ASSERT_TRUE(hasCode(v, "drawings.hlr.shape_approximated"));
    for (const auto& d : v.diagnostics)
        if (d.code == "drawings.hlr.shape_approximated") {
            EXPECT_EQ(d.entityId, "cab1/worktop");
            EXPECT_NE(d.message.find("'under.sink'"), std::string::npos);
        }
}

TEST(ShapedAssemblyView, NoWarningWhenTheHiddenPartIsReallyCovered) {
    domain::Assembly a;
    a.id = domain::AssemblyId("cab1");
    domain::Part top = worktop();
    top.placement = geometry::Transform::translation({0, 0, 100});
    a.parts.push_back(top);
    // Under solid material only: x [100,200], y [200,300] (left of the sink).
    a.parts.push_back(box("under.solid", 100, 100, 18, {100, 200, 0}));
    const View v = generateAssemblyView(a, ViewKind::Top);
    EXPECT_FALSE(hasCode(v, "drawings.hlr.shape_approximated"));
    EXPECT_EQ(countSource(v, "cab1/under.solid", LineStyle::Hidden), 4u);
}

TEST(ShapedAssemblyView, EdgeOnContouredPartKeepsTheExactBoxSilhouette) {
    domain::Assembly a;
    a.id = domain::AssemblyId("cab1");
    domain::Part shelf = roundedShelf();
    shelf.placement = geometry::Transform::translation({0, 0, 300});
    a.parts.push_back(shelf);
    // Front view (u, v) = (x, z): the shelf is edge-on, a 600 x 18 strip; no arcs.
    const View v = generateAssemblyView(a, ViewKind::Front);
    EXPECT_TRUE(v.arcs.empty());
    EXPECT_EQ(v.segments.size(), 4u);
    EXPECT_TRUE(findSegment(v, {0, 300}, {600, 300}));
    EXPECT_TRUE(findSegment(v, {0, 318}, {600, 318}));
    EXPECT_EQ(v.footprints.at(0).aspect, PanelAspect::EdgeOnHorizontal);
    EXPECT_TRUE(v.diagnostics.empty());
}

TEST(ShapedAssemblyView, ArcPartlyHiddenIsSplitExactly) {
    domain::Assembly a;
    a.id = domain::AssemblyId("cab1");
    a.parts.push_back(roundedShelf()); // z 0..18, Top view (u, v) = (x, y)
    // Box above covering x in [0, 30]: the corner arc around (50, 50) (180 -> 270 deg) crosses
    // x = 30 where cos t = (30 - 50) / 50 = -0.4, t = 180 + acos(0.4) = 246.4218 deg
    // (y = 50 - sqrt(2500 - 400) = 4.174 > 0, inside the box's y range [0, 100]).
    a.parts.push_back(box("cover", 30, 100, 18, {0, 0, 50}));
    const View v = generateAssemblyView(a, ViewKind::Top);
    const double split = 180 + core::radToDeg(std::acos(0.4));
    const ViewArc* hidden = nullptr;
    const ViewArc* visible = nullptr;
    for (const ViewArc& arc : v.arcs) {
        if (!samePoint(arc.center, {50, 50})) continue;
        (arc.style == LineStyle::Hidden ? hidden : visible) = &arc;
    }
    ASSERT_TRUE(hidden && visible);
    EXPECT_NEAR(hidden->startDeg, 180, 1e-9);
    EXPECT_NEAR(hidden->endDeg, split, 1e-9);
    EXPECT_NEAR(visible->startDeg, split, 1e-9);
    EXPECT_NEAR(visible->endDeg, 270, 1e-9);
    // The upper-left arc around (50, 350) runs 90 -> 180 deg; x = 30 crossing at
    // t = 113.5782 deg at y = 350 + 45.83 = 395.8, outside the box (y <= 100) -> fully visible.
    for (const ViewArc& arc : v.arcs)
        if (samePoint(arc.center, {50, 350})) {
            EXPECT_EQ(arc.style, LineStyle::Visible);
        }
    // Left outline line (0,50)-(0,350) lies on the box boundary x = 0: not strictly inside ->
    // visible. Coincident pieces merge: y 0..100 belongs to the nearer box, y 100..350 to the shelf.
    const ViewSegment* left = findSegment(v, {0, 100}, {0, 350});
    ASSERT_TRUE(left);
    EXPECT_EQ(left->style, LineStyle::Visible);
    EXPECT_EQ(left->sourceId, "cab1/shelf.round");
    // Omit mode drops the hidden sub-arc.
    ViewOptions o;
    o.hiddenLines = HiddenLineMode::Omit;
    const View vo = generateAssemblyView(a, ViewKind::Top, o);
    for (const ViewArc& arc : vo.arcs) EXPECT_EQ(arc.style, LineStyle::Visible);
}

TEST(ShapedAssemblyView, SectionThroughContouredPartIsReportedAsApproximation) {
    domain::Assembly a;
    a.id = domain::AssemblyId("cab1");
    a.parts.push_back(roundedShelf());
    ViewOptions o;
    o.sectionPosition = 300.0; // Left section: plane x = 300 cuts the shelf
    const View v = generateAssemblyView(a, ViewKind::Left, o);
    EXPECT_EQ(v.cuts.size(), 1u);
    EXPECT_TRUE(hasCode(v, "drawings.section.shape_approximated"));
}

TEST(Hlr, OccluderOnlyBoxAndHideableEdgesAndArcs) {
    // Occluder-only box [0,10] x [0,10] at depth 5; an edge at depth 0 crossing it is split,
    // the box contributes no edges itself.
    HlrBox occ{Rect2{{0, 0}, {10, 10}}, 5, 4, "occ"};
    occ.drawEdges = false;
    const std::vector<ViewSegment> segs =
        removeHiddenLines({occ}, {HlrEdge{{-5, 5}, {15, 5}, 0, "e"}}, {}, HiddenLineMode::Dashed);
    ASSERT_EQ(segs.size(), 3u);
    EXPECT_EQ(segs[0].style, LineStyle::Visible);
    EXPECT_EQ(segs[1].style, LineStyle::Hidden);
    EXPECT_TRUE(samePoint(segs[1].a, {0, 5}) && samePoint(segs[1].b, {10, 5}));
    // Arc around (0,0) r 10 from 0 to 90 deg, box [5, 20] x [-1, 20]: crossing x = 5 at 60 deg.
    const std::vector<ViewArc> arcs = removeHiddenArcs({HlrBox{Rect2{{5, -1}, {20, 20}}, 5, 4, "b"}},
                                                       {HlrArc{{0, 0}, 10, 0, 90, 0, "a"}});
    ASSERT_EQ(arcs.size(), 2u);
    EXPECT_EQ(arcs[0].style, LineStyle::Hidden);
    EXPECT_NEAR(arcs[0].startDeg, 0, 1e-9);
    EXPECT_NEAR(arcs[0].endDeg, 60, 1e-9);
    EXPECT_EQ(arcs[1].style, LineStyle::Visible);
    EXPECT_NEAR(arcs[1].endDeg, 90, 1e-9);
}
