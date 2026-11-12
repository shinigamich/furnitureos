// SPDX-License-Identifier: AGPL-3.0-only
#include "test_util.hpp"

#include <type_traits>

using namespace fos;
using namespace fos::drawings;
using namespace fos::drawings::testing;

namespace {

const GeneratedView* findView(const DrawingResult& r, const std::string& id) {
    for (const GeneratedView& v : r.views)
        if (v.view.id == id) return &v;
    return nullptr;
}

std::size_t countElement(const Sheet& s, const std::string& elementId) {
    std::size_t n = 0;
    for (const Primitive& p : s.primitives) n += elementIdOf(p) == elementId ? 1 : 0;
    return n;
}

/// True when two axis-aligned segments share more than a single end point.
bool linesConflict(const Segment2& a, const Segment2& b) {
    const Rect2 ra = Rect2::fromCorners(a.a, a.b), rb = Rect2::fromCorners(b.a, b.b);
    const double ix = std::min(ra.max.x, rb.max.x) - std::max(ra.min.x, rb.min.x);
    const double iy = std::min(ra.max.y, rb.max.y) - std::max(ra.min.y, rb.min.y);
    if (ix < -1e-9 || iy < -1e-9) return false; // disjoint
    if (ix > 1e-9 || iy > 1e-9) return true;    // collinear overlap of positive length
    // Single common point: a conflict unless it is an end point of both (chain neighbours).
    const Vec2 p{std::max(ra.min.x, rb.min.x), std::max(ra.min.y, rb.min.y)};
    const auto isEnd = [&](const Segment2& s) { return samePoint(s.a, p, 1e-9) || samePoint(s.b, p, 1e-9); };
    return !(isEnd(a) && isEnd(b));
}

} // namespace

TEST(Dimension, ValueIsMeasuredFromTheAnchorsNeverTyped) {
    Dimension d;
    d.orientation = DimensionOrientation::Horizontal;
    d.p1 = {18, 5};
    d.p2 = {782, 300};
    EXPECT_DOUBLE_EQ(measuredValue(d), 764.0); // 782 - 18; the v difference is ignored
    EXPECT_EQ(dimensionText(d), "764");
    d.orientation = DimensionOrientation::Vertical;
    EXPECT_DOUBLE_EQ(measuredValue(d), 295.0);
    d.p2.y = 5 + 719.25;
    EXPECT_EQ(dimensionText(d), "719.3") << "0.1 mm display rounding (half away from zero)";
    EXPECT_EQ(dimensionText(d, 2), "719.25");
    d.textOverride = "H";
    EXPECT_EQ(dimensionText(d), "H");
    EXPECT_DOUBLE_EQ(measuredValue(d), 719.25) << "an override changes the text only";
}

TEST(Dimension, LayoutOfAHorizontalDimension) {
    // 800 mm wide at 1:10, view origin at paper (50, 100), dimension line 9.3 mm below.
    Dimension d;
    d.id = "d";
    d.p1 = {0, 0};
    d.p2 = {800, 0};
    d.offset = -9.3;
    const DimensionStyle st; // gap 1, overshoot 1.5, text gap 0.8, text height 2.5
    const DimensionGeometry g = layoutDimension(d, ViewPlacement{{50, 100}, 0.1}, st);
    EXPECT_TRUE(samePoint(g.line.a, {50, 90.7}) && samePoint(g.line.b, {130, 90.7}));
    ASSERT_TRUE(g.ext1 && g.ext2);
    // Extension lines start 1 mm from the anchor and end 1.5 mm past the dimension line.
    EXPECT_TRUE(samePoint(g.ext1->a, {50, 99}) && samePoint(g.ext1->b, {50, 89.2}));
    EXPECT_TRUE(samePoint(g.ext2->a, {130, 99}) && samePoint(g.ext2->b, {130, 89.2}));
    EXPECT_TRUE(samePoint(g.text.position, {90, 91.5})); // centred, 0.8 above the line
    EXPECT_EQ(g.text.text, "800");
    EXPECT_EQ(g.text.hAlign, HAlign::Center);
    EXPECT_EQ(g.text.rotationDeg, 0.0);
    EXPECT_DOUBLE_EQ(g.text.height, 2.5);
    EXPECT_FALSE(g.terminatorsOutside); // 80 mm span >> 2.5 * arrow
    // Text height does not depend on the scale.
    EXPECT_DOUBLE_EQ(layoutDimension(d, ViewPlacement{{0, 0}, 0.02}, st).text.height, 2.5);

    // Rendered: 2 extension lines + dimension line + 2 arrowheads + text, all tagged with the id.
    std::vector<Primitive> prims;
    renderDimension(d, ViewPlacement{{50, 100}, 0.1}, st, prims);
    ASSERT_EQ(prims.size(), 6u);
    std::size_t lines = 0, arrows = 0, texts = 0;
    for (const Primitive& p : prims) {
        EXPECT_EQ(elementIdOf(p), "d");
        lines += std::holds_alternative<Line>(p) ? 1 : 0;
        arrows += std::holds_alternative<Marker>(p) ? 1 : 0;
        texts += std::holds_alternative<Text>(p) ? 1 : 0;
    }
    EXPECT_EQ(lines, 3u);
    EXPECT_EQ(arrows, 2u);
    EXPECT_EQ(texts, 1u);
    // Arrow tips are on the extension lines and point outwards (180 deg at the left end).
    const Marker& left = std::get<Marker>(prims[3]);
    EXPECT_TRUE(samePoint(left.position, {50, 90.7}));
    EXPECT_NEAR(std::fabs(left.angleDeg), 180.0, 1e-9);
    EXPECT_NEAR(std::get<Marker>(prims[4]).angleDeg, 0.0, 1e-9);
}

TEST(Dimension, LayoutOfAVerticalDimensionAndShortSpan) {
    Dimension d;
    d.orientation = DimensionOrientation::Vertical;
    d.p1 = {0, 0};
    d.p2 = {0, 18};
    d.offset = -6.0;
    d.textRow = 1;
    d.textShift = {0.5, -1.0};
    const DimensionStyle st;
    const DimensionGeometry g = layoutDimension(d, ViewPlacement{{20, 20}, 0.1}, st);
    EXPECT_TRUE(samePoint(g.line.a, {14, 20}) && samePoint(g.line.b, {14, 21.8}));
    // Text left of the line: 0.8 gap + one row (2.5 + 0.8) = 4.1 -> x = 9.9; plus the shift.
    EXPECT_TRUE(samePoint(g.text.position, {9.9 + 0.5, 20.9 - 1.0}));
    EXPECT_EQ(g.text.rotationDeg, 90.0);
    EXPECT_EQ(g.text.text, "18");
    EXPECT_TRUE(g.terminatorsOutside) << "1.8 mm span is shorter than 2.5 arrow lengths";
    // With ticks nothing is flipped.
    DimensionStyle ticks;
    ticks.terminator = DimensionTerminator::Tick;
    EXPECT_FALSE(layoutDimension(d, ViewPlacement{{20, 20}, 0.1}, ticks).terminatorsOutside);
    std::vector<Primitive> prims;
    renderDimension(d, ViewPlacement{{20, 20}, 0.1}, ticks, prims);
    EXPECT_EQ(std::get<Marker>(prims[3]).kind, MarkerKind::Tick);
}

TEST(Dimension, StackingRule) {
    const DimensionStyle st; // firstGap 6, stackGap 3.5, row depth 2.5 + 0.8 = 3.3
    // Horizontal below the view: text is on the view side of each line.
    //   line1 = 6 + 3.3 = 9.3;  line2 = 9.3 + 3.5 + 3.3 = 16.1
    auto h = stackDimensionOffsets({1, 1}, DimensionOrientation::Horizontal, -1, st);
    ASSERT_EQ(h.size(), 2u);
    EXPECT_NEAR(h[0], -9.3, 1e-12);
    EXPECT_NEAR(h[1], -16.1, 1e-12);
    // Vertical left of the view: text is on the far side.
    //   line1 = 6;  line2 = 6 + 3.3 + 3.5 = 12.8;  with two text rows first: 6 + 6.6 + 3.5 = 16.1
    auto v = stackDimensionOffsets({1, 1}, DimensionOrientation::Vertical, -1, st);
    EXPECT_NEAR(v[0], -6.0, 1e-12);
    EXPECT_NEAR(v[1], -12.8, 1e-12);
    v = stackDimensionOffsets({2, 1}, DimensionOrientation::Vertical, -1, st);
    EXPECT_NEAR(v[1], -16.1, 1e-12);
    // Above the view the horizontal text is on the far side.
    h = stackDimensionOffsets({1, 1}, DimensionOrientation::Horizontal, +1, st);
    EXPECT_NEAR(h[0], 6.0, 1e-12);
    EXPECT_NEAR(h[1], 12.8, 1e-12);
}

TEST(AutoDimension, FrontViewValuesEqualTheModel) {
    const domain::Model m = domain::fixtures::basicCabinet();
    const View v = generateAssemblyView(cabinet(m), ViewKind::Front);
    const auto dims = autoDimensionView(v, 0.1, DimensionStyle{});
    const std::string p = "cab1/view.front/";
    // Overall: envelope 800 wide, 720 high.
    const Dimension* w = findDimension(dims, p + "dim.overall.width");
    const Dimension* h = findDimension(dims, p + "dim.overall.height");
    ASSERT_TRUE(w && h);
    EXPECT_DOUBLE_EQ(measuredValue(*w), 800.0);
    EXPECT_DOUBLE_EQ(measuredValue(*h), 720.0);
    EXPECT_EQ(w->orientation, DimensionOrientation::Horizontal);
    EXPECT_EQ(h->orientation, DimensionOrientation::Vertical);
    // Vertical chain through bottom (0..18), shelf (351..369), top (702..720):
    //   18 | 351-18 = 333 | 18 | 702-369 = 333 | 18
    const struct {
        const char* id;
        double value;
    } vertical[] = {{"dim.chain.v[env:min..cab1/bottom:max]", 18},
                    {"dim.chain.v[cab1/bottom:max..cab1/shelf.1:min]", 333},
                    {"dim.chain.v[cab1/shelf.1:min..cab1/shelf.1:max]", 18},
                    {"dim.chain.v[cab1/shelf.1:max..cab1/top:min]", 333},
                    {"dim.chain.v[cab1/top:min..env:max]", 18}};
    for (const auto& e : vertical) {
        const Dimension* d = findDimension(dims, p + e.id);
        ASSERT_TRUE(d) << e.id;
        EXPECT_DOUBLE_EQ(measuredValue(*d), e.value) << e.id;
        EXPECT_NEAR(d->offset, -6.0, 1e-12);
        EXPECT_EQ(d->textRow, 0);
    }
    // Horizontal chain through the sides: 18 | 782-18 = 764 | 18.
    const struct {
        const char* id;
        double value;
    } horizontal[] = {{"dim.chain.h[env:min..cab1/side.left:max]", 18},
                      {"dim.chain.h[cab1/side.left:max..cab1/side.right:min]", 764},
                      {"dim.chain.h[cab1/side.right:min..env:max]", 18}};
    for (const auto& e : horizontal) {
        const Dimension* d = findDimension(dims, p + e.id);
        ASSERT_TRUE(d) << e.id;
        EXPECT_DOUBLE_EQ(measuredValue(*d), e.value) << e.id;
        EXPECT_NEAR(d->offset, -9.3, 1e-12);
    }
    EXPECT_EQ(dims.size(), 10u); // 3 + 1 + 5 + 1
    EXPECT_NEAR(w->offset, -16.1, 1e-12);
    EXPECT_NEAR(h->offset, -12.8, 1e-12);
    // The back panel is seen face-on and contributes nothing (2 and 798 are not boundaries).
    for (const Dimension& d : dims) EXPECT_EQ(d.id.find("cab1/back"), std::string::npos);

    AutoDimensionOptions onlyOverall;
    onlyOverall.chains = false;
    EXPECT_EQ(autoDimensionView(v, 0.1, DimensionStyle{}, onlyOverall).size(), 2u);
}

TEST(AutoDimension, TopAndSideViewsGiveDepth) {
    const domain::Model m = domain::fixtures::basicCabinet();
    const View top = generateAssemblyView(cabinet(m), ViewKind::Top);
    const auto td = autoDimensionView(top, 0.1, DimensionStyle{});
    const Dimension* depth = findDimension(td, "cab1/view.top/dim.overall.depth");
    ASSERT_TRUE(depth);
    EXPECT_DOUBLE_EQ(measuredValue(*depth), 563.0); // 560 carcass + 3 back
    EXPECT_EQ(depth->orientation, DimensionOrientation::Vertical);
    ASSERT_TRUE(findDimension(td, "cab1/view.top/dim.overall.width"));
    // Depth chain: carcass 560, back panel 3.
    const Dimension* carcass = findDimension(td, "cab1/view.top/dim.chain.v[env:min..cab1/back:min]");
    const Dimension* back = findDimension(td, "cab1/view.top/dim.chain.v[cab1/back:min..env:max]");
    ASSERT_TRUE(carcass && back);
    EXPECT_DOUBLE_EQ(measuredValue(*carcass), 560.0);
    EXPECT_DOUBLE_EQ(measuredValue(*back), 3.0);

    const View left = generateAssemblyView(cabinet(m), ViewKind::Left);
    const auto ld = autoDimensionView(left, 0.1, DimensionStyle{});
    const Dimension* d = findDimension(ld, "cab1/view.left/dim.overall.depth");
    ASSERT_TRUE(d);
    EXPECT_DOUBLE_EQ(measuredValue(*d), 563.0);
    EXPECT_EQ(d->orientation, DimensionOrientation::Horizontal);
    EXPECT_DOUBLE_EQ(measuredValue(*findDimension(ld, "cab1/view.left/dim.overall.height")), 720.0);
}

TEST(AutoDimension, NarrowChainTextsMoveToASecondRow) {
    const domain::Model m = domain::fixtures::basicCabinet();
    const View v = generateAssemblyView(cabinet(m), ViewKind::Front);
    // 1:50. Paper boundaries along v: 0, 0.36, 7.02, 7.38, 14.04, 14.4. Estimated text widths
    // incl. 2 x 0.5 margin: "18" = 2*1.5+1 = 4, "333" = 3*1.5+1 = 5.5.
    //   "18"  centre 0.18  -> [-1.82, 2.18]   row 0
    //   "333" centre 3.69  -> [0.94, 6.44]    clashes -> row 1
    //   "18"  centre 7.2   -> [5.2, 9.2]      row 0
    //   "333" centre 10.71 -> [7.96, 13.46]   clashes row 0; row 1 free -> row 1
    //   "18"  centre 14.22 -> [12.22, 16.22]  row 0
    const auto dims = autoDimensionView(v, 0.02, DimensionStyle{});
    const std::string p = "cab1/view.front/dim.chain.v[";
    EXPECT_EQ(findDimension(dims, p + "env:min..cab1/bottom:max]")->textRow, 0);
    EXPECT_EQ(findDimension(dims, p + "cab1/bottom:max..cab1/shelf.1:min]")->textRow, 1);
    EXPECT_EQ(findDimension(dims, p + "cab1/shelf.1:min..cab1/shelf.1:max]")->textRow, 0);
    EXPECT_EQ(findDimension(dims, p + "cab1/shelf.1:max..cab1/top:min]")->textRow, 1);
    EXPECT_EQ(findDimension(dims, p + "cab1/top:min..env:max]")->textRow, 0);
    // Two rows push the overall dimension out: 6 + 2 * 3.3 + 3.5 = 16.1.
    EXPECT_NEAR(findDimension(dims, "cab1/view.front/dim.overall.height")->offset, -16.1, 1e-12);
}

TEST(AutoDimension, DimensionLinesAndTextBoxesDoNotIntersect) {
    const domain::Model m = domain::fixtures::basicCabinet();
    const DimensionStyle st;
    for (const ViewKind kind : {ViewKind::Front, ViewKind::Top, ViewKind::Left, ViewKind::Right, ViewKind::Back}) {
        const View v = generateAssemblyView(cabinet(m), kind);
        for (const double scale : {1.0, 0.5, 0.2, 0.1, 0.05, 0.02, 0.01}) {
            const ViewPlacement pl{{300, 300}, scale};
            const auto dims = autoDimensionView(v, scale, st);
            std::vector<DimensionGeometry> g;
            for (const Dimension& d : dims) g.push_back(layoutDimension(d, pl, st));
            const Rect2 outline{pl.toPaper(v.bounds.min), pl.toPaper(v.bounds.max)};
            const std::string where = std::string(toString(kind)) + " @ " + std::to_string(scale);
            for (std::size_t i = 0; i < g.size(); ++i) {
                EXPECT_FALSE(intersects(g[i].textBounds, outline)) << where << ": text over the view: " << dims[i].id;
                EXPECT_FALSE(segmentCrossesRect(g[i].line, outline)) << where << ": line over the view";
                for (std::size_t j = 0; j < g.size(); ++j) {
                    EXPECT_FALSE(segmentCrossesRect(g[i].line, g[j].textBounds))
                        << where << ": line of " << dims[i].id << " crosses text of " << dims[j].id;
                    if (j <= i) continue;
                    EXPECT_FALSE(intersects(g[i].textBounds, g[j].textBounds))
                        << where << ": texts overlap: " << dims[i].id << " / " << dims[j].id;
                    EXPECT_FALSE(linesConflict(g[i].line, g[j].line))
                        << where << ": lines overlap: " << dims[i].id << " / " << dims[j].id;
                }
            }
        }
    }
}

TEST(Edits, ReappliedByIdAndOrphansReported) {
    domain::Model m = domain::fixtures::basicCabinet();
    const std::string front = "cab1/view.front";
    const std::string gapId = front + "/dim.chain.v[cab1/bottom:max..cab1/shelf.1:min]";
    DrawingEdits edits;
    edits.elements[front + "/dim.overall.width"].offset = -30.0;
    edits.elements[front + "/dim.overall.height"].hidden = true;
    edits.elements[gapId].textShift = {-2.0, 1.0};
    edits.elements[gapId].textOverride = "clear";
    edits.elements[front + "/dim.overall.volume"].hidden = true; // never existed
    edits.dimensions.push_back({"user-dim-1", front, DimensionOrientation::Vertical, {782, 369}, {782, 702}, 15.0, {}});
    edits.texts.push_back({"user-text-1", "cab1/view.top", {400, 280}, "Top", 3.5, 0});
    edits.leaders.push_back({"user-leader-1", front, {400, 360}, {12, 8}, "Shelf", 2.5});
    edits.detailMarkers.push_back({"user-detail-1", front, {18, 18}, 40, "A"});
    edits.texts.push_back({"user-text-2", "cab1/view.nowhere", {0, 0}, "lost", 3.5, 0});

    AssemblyDrawingOptions opt;
    opt.sheet.scale = ScaleRequest::explicitScale(1, 10);
    const auto r1 = generateAssemblyDrawing(m, domain::AssemblyId("cab1"), opt, edits);
    ASSERT_TRUE(r1.ok()) << r1.error().message;
    const DrawingResult& d1 = r1.value();
    const GeneratedView* fv = findView(d1, front);
    ASSERT_TRUE(fv);
    EXPECT_DOUBLE_EQ(findDimension(fv->annotations.dimensions, front + "/dim.overall.width")->offset, -30.0);
    EXPECT_TRUE(findDimension(fv->annotations.dimensions, front + "/dim.overall.height")->hidden);
    const Dimension* gap = findDimension(fv->annotations.dimensions, gapId);
    EXPECT_EQ(gap->textShift, (Vec2{-2.0, 1.0}));
    EXPECT_EQ(dimensionText(*gap), "clear");
    EXPECT_DOUBLE_EQ(measuredValue(*gap), 333.0);
    const Dimension* user = findDimension(fv->annotations.dimensions, "user-dim-1");
    ASSERT_TRUE(user);
    EXPECT_TRUE(user->userAuthored);
    EXPECT_DOUBLE_EQ(measuredValue(*user), 333.0); // 702 - 369

    const Sheet& s1 = d1.sheets.front();
    EXPECT_EQ(countElement(s1, front + "/dim.overall.height"), 0u) << "hidden elements are not drawn";
    EXPECT_EQ(countElement(s1, front + "/dim.overall.width"), 6u);
    EXPECT_EQ(countElement(s1, "user-dim-1"), 6u);
    EXPECT_EQ(countElement(s1, "user-text-1"), 1u);
    EXPECT_EQ(countElement(s1, "user-leader-1"), 4u); // leader line, arrow, shoulder, text
    EXPECT_EQ(countElement(s1, "user-detail-1"), 2u); // circle + label
    EXPECT_EQ(countElement(s1, "user-text-2"), 0u);
    // The width dimension line really is 30 mm below the view: front placement origin.y - 30.
    const ViewPlacement fp = s1.views.front().placement;
    bool found = false;
    for (const Primitive& p : s1.primitives) {
        const Line* l = std::get_if<Line>(&p);
        if (l && l->elementId == front + "/dim.overall.width" && near(l->a.y, l->b.y) &&
            near(std::fabs(l->b.x - l->a.x), 80.0, 1e-9)) {
            EXPECT_NEAR(l->a.y, fp.origin.y - 30.0, 1e-9);
            found = true;
        }
    }
    EXPECT_TRUE(found);

    // Report: orphans are listed and described, never dropped silently.
    const std::vector<std::string> orphans = {front + "/dim.overall.volume", "user-text-2"};
    EXPECT_EQ(d1.edits.orphanIds, orphans);
    EXPECT_EQ(d1.edits.appliedIds.size(), 7u); // 3 element edits + 4 user annotations
    ASSERT_EQ(d1.edits.diagnostics.size(), 2u);
    EXPECT_EQ(d1.edits.diagnostics[0].code, "drawings.edits.orphan");
    EXPECT_EQ(d1.edits.diagnostics[0].entityId, front + "/dim.overall.volume");
    EXPECT_EQ(d1.edits.diagnostics[1].code, "drawings.edits.orphan_annotation");
    EXPECT_NE(d1.edits.diagnostics[1].message.find("cab1/view.nowhere"), std::string::npos);

    // The model changes: the shelf moves up to z = 400. Ids are unchanged, so the edits are
    // re-applied, and the measured value follows the model: 400 - 18 = 382.
    m.assemblies[0].parts[4].placement.origin.z = 400.0;
    const auto r2 = generateAssemblyDrawing(m, domain::AssemblyId("cab1"), opt, edits);
    ASSERT_TRUE(r2.ok());
    const Dimension* gap2 = findDimension(findView(r2.value(), front)->annotations.dimensions, gapId);
    ASSERT_TRUE(gap2);
    EXPECT_DOUBLE_EQ(measuredValue(*gap2), 382.0);
    EXPECT_EQ(gap2->textShift, (Vec2{-2.0, 1.0}));
    EXPECT_EQ(r2.value().edits.orphanIds, orphans);

    // The shelf is removed: its dimension disappears, the edit becomes an orphan, nothing crashes.
    m.assemblies[0].parts.erase(m.assemblies[0].parts.begin() + 4);
    const auto r3 = generateAssemblyDrawing(m, domain::AssemblyId("cab1"), opt, edits);
    ASSERT_TRUE(r3.ok());
    EXPECT_FALSE(findDimension(findView(r3.value(), front)->annotations.dimensions, gapId));
    const std::vector<std::string> orphans3 = {gapId, front + "/dim.overall.volume", "user-text-2"};
    EXPECT_EQ(r3.value().edits.orphanIds, orphans3);
    EXPECT_FALSE(core::hasErrors(r3.value().diagnostics)) << "orphans are warnings";
}

TEST(Edits, MisuseIsReportedNotApplied) {
    const domain::Model m = domain::fixtures::basicCabinet();
    const View v = generateAssemblyView(cabinet(m), ViewKind::Front);
    ViewAnnotations ann;
    ann.dimensions = autoDimensionView(v, 0.1, DimensionStyle{});
    Note n;
    n.id = v.id + "/note.x";
    n.text = "old";
    ann.notes.push_back(n);
    DrawingEdits edits;
    edits.elements[n.id].offset = 5.0;           // notes have no offset
    edits.elements[n.id].textOverride = "new";   // but their text can be replaced
    edits.texts.push_back({"dup", v.id, {0, 0}, "a", 3.5, 0});
    edits.texts.push_back({"dup", v.id, {0, 0}, "b", 3.5, 0});                       // duplicate id
    edits.texts.push_back({v.id + "/dim.overall.width", v.id, {0, 0}, "c", 3.5, 0}); // collides with a generated id
    EditTracker tracker;
    applyEditsToView(v.id, ann, edits, tracker);
    EXPECT_EQ(ann.notes[0].text, "new");
    ASSERT_EQ(ann.notes.size(), 2u);
    EXPECT_EQ(ann.notes[1].text, "a");
    const EditReport report = finishEdits(edits, tracker);
    ASSERT_EQ(report.diagnostics.size(), 3u);
    EXPECT_EQ(report.diagnostics[0].code, "drawings.edits.not_applicable");
    EXPECT_EQ(report.diagnostics[1].code, "drawings.edits.duplicate_id");
    EXPECT_EQ(report.diagnostics[2].code, "drawings.edits.duplicate_id");
    EXPECT_TRUE(report.orphanIds.empty());
}

TEST(Edits, DrawingApiCannotModifyTheModel) {
    // The generators accept the model only by const reference.
    static_assert(std::is_invocable_v<decltype(&generateAssemblyDrawing), const domain::Model&,
                                      const domain::AssemblyId&, const AssemblyDrawingOptions&, const DrawingEdits&>);
    static_assert(std::is_invocable_v<decltype(&generatePartDrawing), const domain::Model&, const domain::PartId&,
                                      const PartDrawingOptions&, const DrawingEdits&>);
    const domain::Model m = domain::fixtures::basicCabinet();
    DrawingEdits edits;
    edits.elements["cab1/view.front/dim.overall.width"].textOverride = "999";
    const auto r = generateAssemblyDrawing(m, domain::AssemblyId("cab1"), {}, edits);
    ASSERT_TRUE(r.ok());
    const Dimension* w =
        findDimension(findView(r.value(), "cab1/view.front")->annotations.dimensions, "cab1/view.front/dim.overall.width");
    EXPECT_EQ(dimensionText(*w), "999");
    EXPECT_DOUBLE_EQ(measuredValue(*w), 800.0);
    EXPECT_DOUBLE_EQ(cabinet(m).bounds().size().x, 800.0);
}
