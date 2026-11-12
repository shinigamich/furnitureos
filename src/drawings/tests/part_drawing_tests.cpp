// SPDX-License-Identifier: AGPL-3.0-only
#include "test_util.hpp"

using namespace fos;
using namespace fos::drawings;
using namespace fos::drawings::testing;

namespace {

const domain::Part& leftSide(const domain::Model& m) { return m.assemblies[0].parts[0]; }

const Note* findNote(const ViewAnnotations& a, const std::string& id) {
    for (const Note& n : a.notes)
        if (n.id == id) return &n;
    return nullptr;
}

const ViewCircle* circleOf(const View& v, const std::string& opId) {
    for (const ViewCircle& c : v.circles)
        if (c.sourceId == opId) return &c;
    return nullptr;
}

std::size_t countSegments(const View& v, const std::string& src, LineStyle style) {
    std::size_t n = 0;
    for (const ViewSegment& s : v.segments) n += (s.sourceId == src && s.style == style) ? 1 : 0;
    return n;
}

const char* const kFront = "cab1/side.left/op.pin.front";
const char* const kBack = "cab1/side.left/op.pin.back";

} // namespace

TEST(PartView, LeftSideHolesAtTruePositions) {
    const domain::Model m = domain::fixtures::basicCabinet();
    const View v = generatePartView(leftSide(m));
    EXPECT_EQ(v.id, "cab1/side.left/view.faceA");
    EXPECT_EQ(v.subject, ViewSubject::Part);
    EXPECT_EQ(v.bounds.min, (Vec2{0, 0}));
    EXPECT_EQ(v.bounds.max, (Vec2{720, 560})); // finished L x W
    EXPECT_TRUE(v.diagnostics.empty());
    // Outline: 4 visible lines owned by the part.
    EXPECT_EQ(countSegments(v, "cab1/side.left", LineStyle::Visible), 4u);
    ASSERT_TRUE(findSegment(v, {0, 0}, {720, 0}));
    // Fixture: two 5 mm x 13 mm holes on face B at (346, 37) and (346, 523). Seen from face A
    // they are on the far face and blind -> dashed circles of radius 2.5 at the same (x, y).
    ASSERT_EQ(v.circles.size(), 2u);
    const ViewCircle* f = circleOf(v, kFront);
    const ViewCircle* b = circleOf(v, kBack);
    ASSERT_TRUE(f && b);
    EXPECT_EQ(f->center, (Vec2{346, 37}));
    EXPECT_EQ(b->center, (Vec2{346, 523}));
    EXPECT_DOUBLE_EQ(f->radius, 2.5);
    EXPECT_EQ(f->style, LineStyle::Hidden);
    // Centre cross: 2 Center-style lines of half-length 1.5 r = 3.75; no "through" diagonals.
    EXPECT_EQ(countSegments(v, kFront, LineStyle::Center), 2u);
    EXPECT_EQ(countSegments(v, kFront, LineStyle::Thin), 0u);
    ASSERT_TRUE(findSegment(v, {346 - 3.75, 37}, {346 + 3.75, 37}));

    // Face B view: v = -y, holes are on the viewed face -> visible.
    PartViewOptions o;
    o.face = PartViewFace::B;
    const View vb = generatePartView(leftSide(m), o);
    EXPECT_EQ(vb.id, "cab1/side.left/view.faceB");
    EXPECT_EQ(vb.bounds.min, (Vec2{0, -560}));
    EXPECT_EQ(vb.bounds.max, (Vec2{720, 0}));
    EXPECT_EQ(circleOf(vb, kFront)->center, (Vec2{346, -37}));
    EXPECT_EQ(circleOf(vb, kBack)->center, (Vec2{346, -523}));
    EXPECT_EQ(circleOf(vb, kFront)->style, LineStyle::Visible);
    EXPECT_EQ(cross(vb.basis.right, vb.basis.up), vb.basis.toViewer);
}

TEST(PartView, AnnotationsOfTheLeftSide) {
    const domain::Model m = domain::fixtures::basicCabinet();
    const domain::Part& part = leftSide(m);
    const View v = generatePartView(part);
    PartViewOptions o;
    o.quantity = 2;
    const ViewAnnotations a = annotatePartView(m, part, v, 0.2, DimensionStyle{}, o);
    const std::string p = "cab1/side.left/view.faceA/";

    // Baseline dimensions from the part origin: x = 346 (both holes), y = 37 and 523; overall 720 x 560.
    ASSERT_EQ(a.dimensions.size(), 5u);
    const Dimension* hx = findDimension(a.dimensions, p + "dim.hole.x[" + kFront + "]");
    const Dimension* hy1 = findDimension(a.dimensions, p + "dim.hole.y[" + kFront + "]");
    const Dimension* hy2 = findDimension(a.dimensions, p + "dim.hole.y[" + kBack + "]");
    const Dimension* len = findDimension(a.dimensions, p + "dim.overall.length");
    const Dimension* wid = findDimension(a.dimensions, p + "dim.overall.width");
    ASSERT_TRUE(hx && hy1 && hy2 && len && wid);
    EXPECT_DOUBLE_EQ(measuredValue(*hx), 346.0);
    EXPECT_DOUBLE_EQ(measuredValue(*hy1), 37.0);
    EXPECT_DOUBLE_EQ(measuredValue(*hy2), 523.0);
    EXPECT_DOUBLE_EQ(measuredValue(*len), 720.0);
    EXPECT_DOUBLE_EQ(measuredValue(*wid), 560.0);
    EXPECT_EQ(hx->p1, (Vec2{0, 0})); // measured from the origin
    EXPECT_EQ(hy2->p1, (Vec2{0, 0}));
    EXPECT_EQ(hy2->p2, (Vec2{0, 523}));
    // Stacked outwards, smallest first (see Dimension.StackingRule): below 9.3 / 16.1; left 6 / 12.8 / 19.6.
    EXPECT_NEAR(hx->offset, -9.3, 1e-12);
    EXPECT_NEAR(len->offset, -16.1, 1e-12);
    EXPECT_NEAR(hy1->offset, -6.0, 1e-12);
    EXPECT_NEAR(hy2->offset, -12.8, 1e-12);
    EXPECT_NEAR(wid->offset, -19.6, 1e-12);

    // Edge banding: L1 = eb.abs2 (E1), W1 and W2 = eb.abs04 (E2); L2 is raw.
    ASSERT_EQ(a.edgeBandMarks.size(), 3u);
    EXPECT_EQ(a.edgeBandMarks[0].id, p + "edge.L1");
    EXPECT_EQ(a.edgeBandMarks[0].code, "E1");
    EXPECT_EQ(a.edgeBandMarks[0].sourceId, "eb.abs2");
    EXPECT_EQ(a.edgeBandMarks[0].a, (Vec2{0, 0}));
    EXPECT_EQ(a.edgeBandMarks[0].b, (Vec2{720, 0}));
    EXPECT_EQ(a.edgeBandMarks[0].inward, (Vec2{0, 1}));
    EXPECT_EQ(a.edgeBandMarks[1].id, p + "edge.W1");
    EXPECT_EQ(a.edgeBandMarks[1].code, "E2");
    EXPECT_EQ(a.edgeBandMarks[1].inward, (Vec2{1, 0}));
    EXPECT_EQ(a.edgeBandMarks[2].id, p + "edge.W2");
    EXPECT_EQ(a.edgeBandMarks[2].code, "E2");
    EXPECT_EQ(a.edgeBandMarks[2].a, (Vec2{720, 0}));
    EXPECT_EQ(a.edgeBandMarks[2].inward, (Vec2{-1, 0}));

    // Grain arrow along the length, in the middle of the part.
    ASSERT_EQ(a.grainArrows.size(), 1u);
    EXPECT_EQ(a.grainArrows[0].center, (Vec2{360, 280}));
    EXPECT_EQ(a.grainArrows[0].direction, (Vec2{1, 0}));

    // Notes. Cut size: 720 - 0.4 - 0.4 = 719.2; 560 - 2 = 558 (domain::cutSize).
    const char* const X = " \xC3\x97 ";
    EXPECT_EQ(findNote(a, p + "title")->text, "Left side");
    EXPECT_DOUBLE_EQ(findNote(a, p + "title")->height, 5.0);
    EXPECT_EQ(findNote(a, p + "note.material")->text, "Material: Particleboard 18 mm");
    EXPECT_EQ(findNote(a, p + "note.finished")->text,
              std::string("Finished size: 720") + X + "560" + X + "18 mm   Qty: 2");
    EXPECT_EQ(findNote(a, p + "note.cutsize")->text, std::string("Cut size: 719.2") + X + "558 mm");
    EXPECT_EQ(findNote(a, p + "note.grain")->text, "Grain: along length");
    EXPECT_EQ(findNote(a, p + "legend.edge[eb.abs2]")->text,
              std::string("E1 = Edge band ABS 2 mm (2") + X + "22 mm): L1");
    EXPECT_EQ(findNote(a, p + "legend.edge[eb.abs04]")->text,
              std::string("E2 = Edge band ABS 0.4 mm (0.4") + X + "22 mm): W1, W2");
    EXPECT_EQ(findNote(a, p + "legend.hole.H1")->text,
              std::string("H1 = Hole \xC3\x98" "5") + X + "13 deep, face B, 2\xC3\x97");
    // Per-hole labels refer to the legend.
    const Note* label = findNote(a, p + "hole[" + kBack + "].label");
    ASSERT_TRUE(label);
    EXPECT_EQ(label->text, "H1");
    EXPECT_EQ(label->sourceId, kBack);
    // Notes are stacked above the outline, title on top, without overlapping (line step 1.5 x height).
    const Note* title = findNote(a, p + "title");
    const Note* material = findNote(a, p + "note.material");
    EXPECT_EQ(title->anchor, (Vec2{0, 560}));
    EXPECT_NEAR(title->paperOffset.y - material->paperOffset.y, 2.5 * 1.5, 1e-12);
    EXPECT_NEAR(findNote(a, p + "legend.hole.H1")->paperOffset.y, 4.0, 1e-12);

    // Localised labels and a missing material are handled.
    domain::Model broken = m;
    broken.materials.clear();
    o.labels.material = "\xD0\x9C\xD0\xB0\xD1\x82\xD0\xB5\xD1\x80\xD1\x96\xD0\xB0\xD0\xBB";
    const ViewAnnotations b = annotatePartView(broken, part, v, 0.2, DimensionStyle{}, o);
    EXPECT_EQ(findNote(b, p + "note.material")->text,
              o.labels.material + ": mat.pb18 (not found in the model)");
    o.holeDimensions = false;
    EXPECT_EQ(annotatePartView(m, part, v, 0.2, DimensionStyle{}, o).dimensions.size(), 2u);
}

TEST(PartView, ThroughEdgeAndGrooveOperations) {
    domain::Part part;
    part.id = domain::PartId("p1");
    part.name = "Test panel";
    part.length = 600;
    part.width = 400;
    part.thickness = 18;
    const auto op = [](const char* id, domain::OperationShape shape) {
        domain::Operation o;
        o.id = domain::OperationId(id);
        o.shape = std::move(shape);
        return o;
    };
    part.operations = {
        op("through", domain::Hole{domain::Face::B, 100, 50, 8, 0, true}),
        op("edgeL1", domain::Hole{domain::Face::L1, 200, 9, 8, 30, false}),
        op("edgeW2", domain::Hole{domain::Face::W2, 300, 9, 8, 30, false}),
        op("grooveB", domain::Groove{domain::Face::B, 0, 380, 600, 380, 4, 8}),
        op("pocketA", domain::Pocket{domain::Face::A, {{10, 10}, {40, 10}, {40, 30}}, 5, false}),
        op("edgePocket", domain::Pocket{domain::Face::L2, {{10, 2}, {40, 2}, {40, 9}}, 5, false}),
        op("badHole", domain::Hole{domain::Face::A, 50, 50, 0, 5, false}),
    };
    const View v = generatePartView(part);
    // Through hole drilled from B: visible from A, with the two thin diagonals of length 2 r.
    ASSERT_EQ(v.circles.size(), 1u);
    EXPECT_EQ(v.circles[0].style, LineStyle::Visible);
    EXPECT_EQ(v.circles[0].center, (Vec2{100, 50}));
    EXPECT_EQ(countSegments(v, "through", LineStyle::Thin), 2u);
    // Edge hole on L1 at u = 200, dia 8, 30 deep: hidden bore x [196,204], y [0,30] + axis.
    EXPECT_EQ(countSegments(v, "edgeL1", LineStyle::Hidden), 4u);
    ASSERT_TRUE(findSegment(v, {196, 30}, {204, 30}));
    ASSERT_TRUE(findSegment(v, {196, 0}, {196, 30}));
    const ViewSegment* axis = findSegment(v, {200, 0}, {200, 35}); // 30 + r + 1
    ASSERT_TRUE(axis);
    EXPECT_EQ(axis->style, LineStyle::Center);
    // Edge hole on W2 (x = 600) at u = y = 300: bore x [570,600], y [296,304].
    ASSERT_TRUE(findSegment(v, {570, 296}, {570, 304}));
    ASSERT_TRUE(findSegment(v, {565, 300}, {600, 300}));
    // Groove on the far face B, centre line y = 380, 4 wide: hidden lines at y = 378 and 382.
    EXPECT_EQ(countSegments(v, "grooveB", LineStyle::Hidden), 4u);
    ASSERT_TRUE(findSegment(v, {0, 378}, {600, 378}));
    ASSERT_TRUE(findSegment(v, {0, 382}, {600, 382}));
    // Pocket on the viewed face: closed visible triangle.
    EXPECT_EQ(countSegments(v, "pocketA", LineStyle::Visible), 3u);
    ASSERT_TRUE(findSegment(v, {40, 30}, {10, 10}));
    // Unsupported / invalid operations are reported, not silently dropped.
    ASSERT_EQ(v.diagnostics.size(), 2u);
    EXPECT_EQ(v.diagnostics[0].code, "drawings.part.unsupported_operation");
    EXPECT_EQ(v.diagnostics[0].entityId, "edgePocket");
    EXPECT_EQ(v.diagnostics[1].code, "drawings.part.invalid_operation");
    EXPECT_NE(v.diagnostics[1].message.find("Test panel"), std::string::npos);

    // Seen from B the groove is visible and mirrored in v.
    PartViewOptions o;
    o.face = PartViewFace::B;
    const View vb = generatePartView(part, o);
    EXPECT_EQ(countSegments(vb, "grooveB", LineStyle::Visible), 4u);
    ASSERT_TRUE(findSegment(vb, {0, -378}, {600, -378}));
    EXPECT_EQ(countSegments(vb, "pocketA", LineStyle::Hidden), 3u);

    // Dimensions: x = 100 (through), 200 (edge L1); y = 50, 300 (edge W2, measured along the width).
    domain::Model model;
    const ViewAnnotations a = annotatePartView(model, part, v, 0.2, DimensionStyle{});
    const std::string p = "p1/view.faceA/";
    EXPECT_DOUBLE_EQ(measuredValue(*findDimension(a.dimensions, p + "dim.hole.x[through]")), 100.0);
    EXPECT_DOUBLE_EQ(measuredValue(*findDimension(a.dimensions, p + "dim.hole.x[edgeL1]")), 200.0);
    EXPECT_DOUBLE_EQ(measuredValue(*findDimension(a.dimensions, p + "dim.hole.y[through]")), 50.0);
    EXPECT_DOUBLE_EQ(measuredValue(*findDimension(a.dimensions, p + "dim.hole.y[edgeW2]")), 300.0);
    EXPECT_EQ(a.dimensions.size(), 6u);
    EXPECT_TRUE(a.grainArrows.empty());
    EXPECT_TRUE(a.edgeBandMarks.empty());
    // Three hole groups: through dia 8 on B, dia 8 x 30 on L1, dia 8 x 30 on W2.
    EXPECT_EQ(findNote(a, p + "legend.hole.H1")->text, "H1 = Hole \xC3\x98" "8 through, face B, 1\xC3\x97");
    ASSERT_TRUE(findNote(a, p + "legend.hole.H3"));
    EXPECT_FALSE(findNote(a, p + "legend.hole.H4"));
}

TEST(PartDrawing, SheetForTheLeftSide) {
    const domain::Model m = domain::fixtures::basicCabinet();
    PartDrawingOptions opt; // A3 landscape, fit to page
    opt.view.quantity = 1;
    const auto r = generatePartDrawing(m, domain::PartId("cab1/side.left"), opt);
    ASSERT_TRUE(r.ok()) << r.error().message;
    ASSERT_EQ(r.value().sheets.size(), 1u);
    const Sheet& s = r.value().sheets[0];
    // 720 x 560 at 1:5 = 144 x 112 plus dimensions and notes fits the 390 x 249 area; at 1:2
    // the part alone is 280 mm high and does not.
    EXPECT_EQ(s.scale.text(), "1:5");
    EXPECT_EQ(s.id, "cab1/side.left/sheet");
    ASSERT_EQ(s.views.size(), 1u);
    const ViewPlacement pl = s.views[0].placement;
    EXPECT_DOUBLE_EQ(pl.scale, 0.2);
    EXPECT_TRUE(s.drawingArea.contains(s.views[0].paperBounds.min, 1e-9));
    EXPECT_TRUE(s.drawingArea.contains(s.views[0].paperBounds.max, 1e-9));
    // Holes: circles of radius 2.5 / 5 = 0.5 mm at origin + (346, 37) / 5 and (346, 523) / 5, dashed.
    std::size_t holes = 0;
    for (const Primitive& p : s.primitives) {
        const Circle* c = std::get_if<Circle>(&p);
        if (!c) continue;
        ++holes;
        EXPECT_DOUBLE_EQ(c->radius, 0.5);
        EXPECT_EQ(c->style, LineStyle::Hidden);
        const double y = c->sourceId == kFront ? 37.0 : 523.0;
        EXPECT_TRUE(samePoint(c->center, pl.origin + Vec2{346.0 / 5, y / 5}, 1e-9)) << c->sourceId;
    }
    EXPECT_EQ(holes, 2u);
    // Edge-band mark of L1: thick line 1.2 mm inside the bottom edge, shortened by 1.2 at both ends.
    bool mark = false, title = false;
    for (const Primitive& p : s.primitives) {
        if (const Line* l = std::get_if<Line>(&p); l && l->elementId == "cab1/side.left/view.faceA/edge.L1") {
            mark = true;
            EXPECT_DOUBLE_EQ(l->weight, 1.0);
            EXPECT_EQ(l->sourceId, "eb.abs2");
            EXPECT_TRUE(samePoint(l->a, pl.origin + Vec2{1.2, 1.2}, 1e-9));
            EXPECT_TRUE(samePoint(l->b, pl.origin + Vec2{144 - 1.2, 1.2}, 1e-9));
        }
        if (const Text* t = std::get_if<Text>(&p); t && t->elementId == "sheet/title.title") {
            title = true;
            EXPECT_EQ(t->text, "Left side");
        }
    }
    EXPECT_TRUE(mark);
    EXPECT_TRUE(title);

    // Edits apply to part drawings too; a wrong id is reported.
    DrawingEdits edits;
    edits.elements["cab1/side.left/view.faceA/grain"].hidden = true;
    edits.elements["cab1/side.left/view.faceA/dim.hole.x[gone]"].hidden = true;
    const auto e = generatePartDrawing(m, domain::PartId("cab1/side.left"), opt, edits);
    ASSERT_TRUE(e.ok());
    for (const Primitive& p : e.value().sheets[0].primitives)
        EXPECT_NE(elementIdOf(p), "cab1/side.left/view.faceA/grain");
    ASSERT_EQ(e.value().edits.orphanIds.size(), 1u);
    EXPECT_EQ(e.value().edits.orphanIds[0], "cab1/side.left/view.faceA/dim.hole.x[gone]");

    // A4 landscape (area 267 x 138): 1:5 needs 112 mm + dimensions + notes > 138 -> 1:10.
    opt.sheet.format = PaperFormat::A4;
    const auto a4 = generatePartDrawing(m, domain::PartId("cab1/side.left"), opt);
    ASSERT_TRUE(a4.ok()) << a4.error().message;
    EXPECT_EQ(a4.value().sheets[0].scale.text(), "1:10");
}

TEST(PartDrawing, Errors) {
    domain::Model m = domain::fixtures::basicCabinet();
    auto r = generatePartDrawing(m, domain::PartId("cab1/nope"));
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, core::ErrorCode::NotFound);
    EXPECT_NE(r.error().message.find("cab1/nope"), std::string::npos);
    m.assemblies[0].parts[0].width = 0;
    r = generatePartDrawing(m, domain::PartId("cab1/side.left"));
    ASSERT_FALSE(r.ok());
    EXPECT_NE(r.error().message.find("Left side"), std::string::npos);
    EXPECT_NE(r.error().message.find("720 x 0 x 18"), std::string::npos);
}
