// SPDX-License-Identifier: AGPL-3.0-only
#include "test_util.hpp"

using namespace fos;
using namespace fos::drawings;
using namespace fos::drawings::testing;

namespace {

const Text* findText(const Sheet& s, const std::string& elementId) {
    for (const Primitive& p : s.primitives)
        if (const Text* t = std::get_if<Text>(&p); t && t->elementId == elementId) return t;
    return nullptr;
}

const PlacedView* placed(const Sheet& s, const std::string& id) {
    for (const PlacedView& v : s.views)
        if (v.viewId == id) return &v;
    return nullptr;
}

AssemblyDrawingOptions onPaper(PaperFormat f, PaperOrientation o = PaperOrientation::Landscape) {
    AssemblyDrawingOptions opt;
    opt.sheet.format = f;
    opt.sheet.orientation = o;
    return opt;
}

} // namespace

TEST(Paper, SizesAndOrientation) {
    const struct {
        PaperFormat f;
        double w, h;
    } rows[] = {{PaperFormat::A4, 210, 297}, {PaperFormat::A3, 297, 420}, {PaperFormat::A2, 420, 594},
                {PaperFormat::A1, 594, 841}};
    for (const auto& r : rows) {
        const auto p = paperSize(r.f, PaperOrientation::Portrait);
        ASSERT_TRUE(p.ok());
        EXPECT_EQ(p.value().width, r.w);
        EXPECT_EQ(p.value().height, r.h);
        const auto l = paperSize(r.f, PaperOrientation::Landscape);
        EXPECT_EQ(l.value().width, r.h);
        EXPECT_EQ(l.value().height, r.w);
    }
    const auto c = paperSize(PaperFormat::Custom, PaperOrientation::Landscape, {300, 500});
    ASSERT_TRUE(c.ok());
    EXPECT_EQ(c.value().width, 500);
    EXPECT_EQ(c.value().height, 300);
    const auto bad = paperSize(PaperFormat::Custom, PaperOrientation::Portrait, {0, 500});
    ASSERT_FALSE(bad.ok());
    EXPECT_EQ(bad.error().code, core::ErrorCode::InvalidArgument);
    EXPECT_STREQ(toString(PaperFormat::A3), "A3");
}

TEST(Scale, TextAndStandardSeries) {
    EXPECT_EQ((Scale{1, 10}).text(), "1:10");
    EXPECT_EQ((Scale{2, 1}).text(), "2:1");
    EXPECT_EQ((Scale{1, 7.5}).text(), "1:7.5");
    EXPECT_DOUBLE_EQ((Scale{1, 20}).factor(), 0.05);
    const double expected[] = {1, 2, 5, 10, 20, 50, 100};
    ASSERT_EQ(standardScales().size(), 7u);
    for (std::size_t i = 0; i < 7; ++i) {
        EXPECT_EQ(standardScales()[i].numerator, 1);
        EXPECT_EQ(standardScales()[i].denominator, expected[i]);
    }
}

TEST(Sheet, FrameTitleBlockAndCalibrationOnA3) {
    SheetOptions o; // A3 landscape, margins 20/10/10/10
    o.title = {"Kitchen \xD0\x9A\xD1\x83\xD1\x85\xD0\xBD\xD1\x8F", "Base cabinet", "A. Author", "2026-11-12", 2, 3};
    const auto r = makeSheet(o, Scale{1, 10}, "s1");
    ASSERT_TRUE(r.ok()) << r.error().message;
    const Sheet& s = r.value();
    EXPECT_EQ(s.id, "s1");
    EXPECT_EQ(s.width, 420);
    EXPECT_EQ(s.height, 297);
    EXPECT_EQ(s.frame.min, (Vec2{20, 10}));
    EXPECT_EQ(s.frame.max, (Vec2{410, 287}));
    // Frame 390 wide, title block 180 -> 210 mm free: calibration beside it. The strip is
    // max(28, calibration 16 + 2 * 4) = 28 high, so the drawing area is 390 x 249.
    EXPECT_EQ(s.drawingArea.min, (Vec2{20, 38}));
    EXPECT_EQ(s.drawingArea.max, (Vec2{410, 287}));
    const Polyline& frame = std::get<Polyline>(s.primitives.front());
    EXPECT_EQ(frame.elementId, "sheet/frame");
    EXPECT_EQ(frame.style, LineStyle::Border);
    EXPECT_TRUE(frame.closed);

    EXPECT_EQ(findText(s, "sheet/title.scale")->text, "1:10");
    EXPECT_EQ(findText(s, "sheet/title.sheet")->text, "2/3");
    EXPECT_EQ(findText(s, "sheet/title.date")->text, "2026-11-12");
    EXPECT_EQ(findText(s, "sheet/title.author")->text, "A. Author");
    EXPECT_EQ(findText(s, "sheet/title.title")->text, "Base cabinet");
    EXPECT_EQ(findText(s, "sheet/title.project")->text, o.title.project);
    EXPECT_EQ(findText(s, "sheet/title.scale.label")->text, "Scale");
    // All title-block texts are inside the title block (230..410 x 10..38).
    const Rect2 tb{{230, 10}, {410, 38}};
    for (const Primitive& p : s.primitives)
        if (const Text* t = std::get_if<Text>(&p); t && t->elementId.rfind("sheet/title.", 0) == 0) {
            EXPECT_TRUE(tb.contains(t->position)) << t->elementId;
        }

    // Calibration: one line exactly 100 mm long, 11 ticks 10 mm apart, label "100 mm", 2 notes.
    std::size_t lines = 0, ticks = 0, notes = 0;
    double x0 = 0;
    for (const Primitive& p : s.primitives) {
        if (const Line* l = std::get_if<Line>(&p)) {
            if (l->elementId == "sheet/calibration.line") {
                ++lines;
                x0 = l->a.x;
                EXPECT_DOUBLE_EQ(l->b.x - l->a.x, 100.0);
                EXPECT_DOUBLE_EQ(l->a.y, l->b.y);
            } else if (l->elementId == "sheet/calibration.tick") {
                EXPECT_DOUBLE_EQ(l->a.x, 24.0 + 10.0 * static_cast<double>(ticks));
                ++ticks;
            }
        } else if (const Text* t = std::get_if<Text>(&p); t && t->elementId == "sheet/calibration.note") {
            EXPECT_EQ(t->text, o.calibration.noteLines[notes]);
            ++notes;
        }
    }
    EXPECT_EQ(lines, 1u);
    EXPECT_EQ(ticks, 11u);
    EXPECT_EQ(notes, 2u);
    EXPECT_DOUBLE_EQ(x0, 24.0); // frame left 20 + 4 padding
    EXPECT_EQ(findText(s, "sheet/calibration.label")->text, "100 mm");
}

TEST(Sheet, LocalisedLabelsRectangleCalibrationAndA4Portrait) {
    SheetOptions o;
    o.format = PaperFormat::A4;
    o.orientation = PaperOrientation::Portrait;
    o.labels.scale = "\xD0\x9C\xD0\xB0\xD1\x81\xD1\x88\xD1\x82\xD0\xB0\xD0\xB1"; // "Масштаб"
    o.calibration.kind = CalibrationKind::Rectangle10050;
    o.calibration.unit = "\xD0\xBC\xD0\xBC"; // "мм"
    o.calibration.noteLines = {"\xD0\x94\xD1\x80\xD1\x83\xD0\xBA 100 %"};
    const auto r = makeSheet(o, Scale{1, 5});
    ASSERT_TRUE(r.ok()) << r.error().message;
    const Sheet& s = r.value();
    // Frame 180 x 277: no room beside the 180 mm title block, so the calibration block
    // (1 note 3.5 + 2 + 50 = 55.5) is stacked above it: strip = 28 + 55.5 + 8 = 91.5.
    EXPECT_EQ(s.frame.max, (Vec2{200, 287}));
    EXPECT_DOUBLE_EQ(s.drawingArea.min.y, 10 + 91.5);
    EXPECT_EQ(findText(s, "sheet/title.scale.label")->text, o.labels.scale);
    EXPECT_EQ(findText(s, "sheet/calibration.label")->text, "100 \xC3\x97 50 \xD0\xBC\xD0\xBC");
    const Polyline* rect = nullptr;
    for (const Primitive& p : s.primitives)
        if (const Polyline* pl = std::get_if<Polyline>(&p); pl && pl->elementId == "sheet/calibration.rect") rect = pl;
    ASSERT_TRUE(rect);
    ASSERT_EQ(rect->points.size(), 4u);
    EXPECT_DOUBLE_EQ(rect->points[1].x - rect->points[0].x, 100.0);
    EXPECT_DOUBLE_EQ(rect->points[2].y - rect->points[1].y, 50.0);
    EXPECT_GE(rect->points[0].y, 38.0) << "above the title block";

    o.calibration.kind = CalibrationKind::None;
    const auto none = makeSheet(o, Scale{1, 5});
    ASSERT_TRUE(none.ok());
    EXPECT_DOUBLE_EQ(none.value().drawingArea.min.y, 38.0);
    EXPECT_FALSE(findText(none.value(), "sheet/calibration.label"));
}

TEST(Sheet, InvalidInputIsRejected) {
    SheetOptions o;
    o.format = PaperFormat::Custom;
    o.custom = {90, 90};
    auto r = makeSheet(o, Scale{1, 1});
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, core::ErrorCode::InvalidArgument);
    EXPECT_NE(r.error().message.find("90 x 90"), std::string::npos);
    o = {};
    o.margins.left = -1;
    EXPECT_FALSE(makeSheet(o, Scale{1, 1}).ok());
    o = {};
    EXPECT_FALSE(makeSheet(o, Scale{0, 1}).ok());
    EXPECT_FALSE(makeSheet(o, Scale{1, 0}).ok());
}

// Paper size needed by the three views of the fixture at scale s (first angle), by hand.
// Annotated boxes relative to each view origin:
//   below: overall line at 16.1 + 1.5 extension overshoot = 17.6;
//   left:  overall line at 12.8, its rotated text reaches 12.8 + 0.8 + 0.75 * 2.5 = 15.475;
//   the 18 mm / 3 mm chain dimensions at the ends are narrower than 2.5 arrow lengths, so their
//   arrows sit outside on a tail of 1.6 * 2.5 = 4 mm that sticks out past the view outline:
//   front: 4 right, 4 above;  top: 4 right, 4 above (3 mm back);  left view: 4 above, and its
//   3 mm chain end is on the left where the 15.475 of the vertical stack already reaches farther.
//   height = (720 s + 17.6 + 4) + 12 + (563 s + 17.6 + 4)           = 1283 s + 55.2
//   width  = (800 s + 15.475 + 4) + 12 + (563 s + 15.475)           = 1363 s + 46.95
//   1:5  -> 319.55 x 311.8     1:10 -> 183.25 x 183.5     1:20 -> 115.1 x 119.35

TEST(AssemblyDrawing, FitToPagePicksTheLargestStandardScale) {
    const domain::Model m = domain::fixtures::basicCabinet();
    const domain::AssemblyId id("cab1");
    // A3 landscape: drawing area 390 x 249. 1:5 needs 311.8 mm of height -> 1:10.
    auto r = generateAssemblyDrawing(m, id, onPaper(PaperFormat::A3));
    ASSERT_TRUE(r.ok()) << r.error().message;
    ASSERT_EQ(r.value().sheets.size(), 1u);
    const Sheet& a3 = r.value().sheets[0];
    EXPECT_EQ(a3.scale.text(), "1:10");
    EXPECT_EQ(findText(a3, "sheet/title.scale")->text, "1:10") << "the chosen scale is recorded on the sheet";
    EXPECT_EQ(findText(a3, "sheet/title.title")->text, "Base cabinet 800");
    EXPECT_EQ(findText(a3, "sheet/title.sheet")->text, "1/1");
    // A2 landscape: area 564 x 372 -> 1:5 fits (311.8 high), 1:2 (720 * 0.5 + ... > 372) does not.
    r = generateAssemblyDrawing(m, id, onPaper(PaperFormat::A2));
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(r.value().sheets[0].scale.text(), "1:5");
    // A4 portrait: area 180 x 225 (strip 28 + 16 + 8 = 52). 1:10 is 183.25 wide -> 3.25 mm too
    // wide, so the next standard scale 1:20 is taken (never an in-between scale such as 1:10.2).
    r = generateAssemblyDrawing(m, id, onPaper(PaperFormat::A4, PaperOrientation::Portrait));
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(r.value().sheets[0].scale.text(), "1:20");
    // A4 landscape: area 267 x 138 -> 1:10 is too high (183.5); 1:20 (119.35) fits.
    r = generateAssemblyDrawing(m, id, onPaper(PaperFormat::A4));
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(r.value().sheets[0].scale.text(), "1:20");
    EXPECT_DOUBLE_EQ(r.value().sheets[0].views[0].placement.scale, 0.05);
}

TEST(AssemblyDrawing, ExplicitScaleIsExactOrAnError) {
    const domain::Model m = domain::fixtures::basicCabinet();
    const domain::AssemblyId id("cab1");
    AssemblyDrawingOptions opt = onPaper(PaperFormat::A3);
    opt.sheet.scale = ScaleRequest::explicitScale(1, 5);
    auto r = generateAssemblyDrawing(m, id, opt);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, core::ErrorCode::ConstraintViolation);
    // The message names the scale, the need and the available area.
    EXPECT_NE(r.error().message.find("1:5"), std::string::npos) << r.error().message;
    EXPECT_NE(r.error().message.find("319.6 x 311.8"), std::string::npos) << r.error().message;
    EXPECT_NE(r.error().message.find("390 x 249"), std::string::npos) << r.error().message;
    EXPECT_NE(r.error().message.find("Base cabinet 800"), std::string::npos);

    // A custom scale 1:8 fits (height 1283 / 8 + 55.2 = 215.575) and is used as is.
    opt.sheet.scale = ScaleRequest::explicitScale(1, 8);
    r = generateAssemblyDrawing(m, id, opt);
    ASSERT_TRUE(r.ok()) << r.error().message;
    const Sheet& s = r.value().sheets[0];
    EXPECT_EQ(s.scale.text(), "1:8");
    EXPECT_EQ(findText(s, "sheet/title.scale")->text, "1:8");
    // The bottom panel's front edge (764 mm) measures 764 / 8 = 95.5 mm on paper.
    bool found = false;
    for (const Primitive& p : s.primitives)
        if (const Line* l = std::get_if<Line>(&p);
            l && l->sourceId == "cab1/bottom" && near(l->a.y, l->b.y) && near(std::fabs(l->b.x - l->a.x), 95.5))
            found = true;
    EXPECT_TRUE(found);

    opt.sheet.scale = ScaleRequest::explicitScale(0, 8);
    r = generateAssemblyDrawing(m, id, opt);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, core::ErrorCode::InvalidArgument);
    // Nothing fits on a 150 x 150 sheet even at 1:100? It does (8 x 7.2 mm views); but a huge
    // model does not: scale the cabinet width to 60 m.
    domain::Model big = m;
    big.assemblies[0].parts[2].length = 60000;
    r = generateAssemblyDrawing(big, id, onPaper(PaperFormat::A4));
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, core::ErrorCode::ConstraintViolation);
    EXPECT_NE(r.error().message.find("1:100"), std::string::npos);
}

TEST(AssemblyDrawing, ThreeViewArrangementFirstAndThirdAngle) {
    const domain::Model m = domain::fixtures::basicCabinet();
    const domain::AssemblyId id("cab1");
    AssemblyDrawingOptions opt = onPaper(PaperFormat::A3);
    auto r = generateAssemblyDrawing(m, id, opt);
    ASSERT_TRUE(r.ok());
    {
        const Sheet& s = r.value().sheets[0];
        ASSERT_EQ(s.views.size(), 3u);
        const PlacedView* f = placed(s, "cab1/view.front");
        const PlacedView* t = placed(s, "cab1/view.top");
        const PlacedView* l = placed(s, "cab1/view.left");
        ASSERT_TRUE(f && t && l) << "first angle uses the LEFT view on the right";
        EXPECT_DOUBLE_EQ(t->placement.origin.x, f->placement.origin.x) << "top view aligned with the front view";
        EXPECT_DOUBLE_EQ(l->placement.origin.y, f->placement.origin.y) << "side view aligned with the front view";
        EXPECT_NEAR(f->paperBounds.min.y - t->paperBounds.max.y, 12.0, 1e-9) << "top view BELOW, 12 mm gap";
        EXPECT_NEAR(l->paperBounds.min.x - f->paperBounds.max.x, 12.0, 1e-9) << "side view to the right";
        for (const PlacedView& v : s.views) {
            EXPECT_DOUBLE_EQ(v.placement.scale, 0.1);
            EXPECT_TRUE(s.drawingArea.contains(v.paperBounds.min, 1e-9) && s.drawingArea.contains(v.paperBounds.max, 1e-9));
        }
        // Hand-computed box of the front view: [-15.475, 80 + 4] x [-17.6, 72 + 4] around its origin.
        EXPECT_NEAR(f->paperBounds.min.x, f->placement.origin.x - 15.475, 1e-9);
        EXPECT_NEAR(f->paperBounds.min.y, f->placement.origin.y - 17.6, 1e-9);
        EXPECT_NEAR(f->paperBounds.max.x, f->placement.origin.x + 84.0, 1e-9);
        EXPECT_NEAR(f->paperBounds.max.y, f->placement.origin.y + 76.0, 1e-9);
        // The whole arrangement (183.25 x 183.5) is centred in the drawing area.
        Rect2 total;
        for (const PlacedView& v : s.views) total.expand(v.paperBounds);
        EXPECT_NEAR(total.width(), 183.25, 1e-9);
        EXPECT_NEAR(total.height(), 183.5, 1e-9);
        EXPECT_TRUE(samePoint(total.center(), s.drawingArea.center(), 1e-9));
        // paper <-> view conversion round trip for UI picking.
        EXPECT_TRUE(samePoint(f->placement.toView(f->placement.toPaper({400, 360})), {400, 360}, 1e-9));
    }
    opt.projection = ProjectionMethod::ThirdAngle;
    r = generateAssemblyDrawing(m, id, opt);
    ASSERT_TRUE(r.ok());
    {
        const Sheet& s = r.value().sheets[0];
        const PlacedView* f = placed(s, "cab1/view.front");
        const PlacedView* t = placed(s, "cab1/view.top");
        const PlacedView* side = placed(s, "cab1/view.right");
        ASSERT_TRUE(f && t && side) << "third angle uses the RIGHT view on the right";
        EXPECT_NEAR(t->paperBounds.min.y - f->paperBounds.max.y, 12.0, 1e-9) << "top view ABOVE";
        EXPECT_GT(side->paperBounds.min.x, f->paperBounds.max.x);
    }
    opt.topView = false;
    opt.sideView = false;
    opt.autoDimensions = false;
    r = generateAssemblyDrawing(m, id, opt);
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(r.value().sheets[0].views.size(), 1u);
    EXPECT_EQ(r.value().sheets[0].scale.text(), "1:5") << "a single undimensioned 160 x 144 view fits at 1:5";
}

TEST(AssemblyDrawing, SectionsGetTheirOwnNumberedSheets) {
    const domain::Model m = domain::fixtures::basicCabinet();
    AssemblyDrawingOptions opt = onPaper(PaperFormat::A3);
    opt.sections.push_back({ViewKind::Left, 400.0, ""});
    const auto r = generateAssemblyDrawing(m, domain::AssemblyId("cab1"), opt);
    ASSERT_TRUE(r.ok()) << r.error().message;
    ASSERT_EQ(r.value().sheets.size(), 2u);
    EXPECT_EQ(findText(r.value().sheets[0], "sheet/title.sheet")->text, "1/2");
    EXPECT_EQ(findText(r.value().sheets[1], "sheet/title.sheet")->text, "2/2");
    EXPECT_EQ(r.value().sheets[1].id, "cab1/sheet.section.1");
    ASSERT_EQ(r.value().sheets[1].views.size(), 1u);
    EXPECT_EQ(r.value().sheets[1].views[0].viewId, "cab1/section.left");
    // 563 x 720 section alone on A3: 1:5 needs 144 + 17.6 + 4 = 165.6 mm of height -> fits; 1:2 does not.
    EXPECT_EQ(r.value().sheets[1].scale.text(), "1:5");
    ASSERT_EQ(r.value().views.size(), 4u);
    EXPECT_EQ(r.value().views[3].sheetIndex, 1u);
    EXPECT_EQ(r.value().views[3].view.cuts.size(), 4u);
    // Hatch lines are on the sheet.
    std::size_t hatch = 0;
    for (const Primitive& p : r.value().sheets[1].primitives)
        if (const Line* l = std::get_if<Line>(&p); l && l->style == LineStyle::Thin && !l->sourceId.empty()) ++hatch;
    EXPECT_GT(hatch, 50u);

    // Looking along +X, a plane at x = 900 has the whole cabinet on the viewer's side: nothing
    // is left to draw -> explicit error, no empty sheet.
    opt.sections[0].position = 900.0;
    const auto e = generateAssemblyDrawing(m, domain::AssemblyId("cab1"), opt);
    ASSERT_FALSE(e.ok());
    EXPECT_NE(e.error().message.find("900"), std::string::npos);
}

TEST(AssemblyDrawing, UnknownAssemblyAndDiagnostics) {
    domain::Model m = domain::fixtures::basicCabinet();
    const auto r = generateAssemblyDrawing(m, domain::AssemblyId("nope"));
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, core::ErrorCode::NotFound);
    EXPECT_NE(r.error().message.find("nope"), std::string::npos);
    // HLR fallbacks of every view surface in the result.
    m.assemblies[0].parts[4].placement =
        geometry::Transform::translation({18, 20, 351}) * geometry::Transform::rotationY(-10);
    const auto d = generateAssemblyDrawing(m, domain::AssemblyId("cab1"));
    ASSERT_TRUE(d.ok());
    std::size_t n = 0;
    for (const core::Diagnostic& diag : d.value().diagnostics) n += diag.code == "drawings.hlr.not_axis_aligned" ? 1 : 0;
    EXPECT_EQ(n, 3u); // once per view
    m.assemblies[0].parts.clear();
    EXPECT_FALSE(generateAssemblyDrawing(m, domain::AssemblyId("cab1")).ok());
}
