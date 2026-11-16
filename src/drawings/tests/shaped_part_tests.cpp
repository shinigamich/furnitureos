// SPDX-License-Identifier: AGPL-3.0-only
// Part drawings of contoured parts (phase 12, ADR-0008). Expected numbers are derived by hand
// from the contour definitions in shaped_fixtures.hpp.
#include "fos/drawings/dxf.hpp"
#include "fos/drawings/svg.hpp"
#include "shaped_fixtures.hpp"

#include <cstdlib>
#include <sstream>

using namespace fos;
using namespace fos::drawings;
using namespace fos::drawings::testing;

namespace {

const Note* findNote(const ViewAnnotations& a, const std::string& id) {
    for (const Note& n : a.notes)
        if (n.id == id) return &n;
    return nullptr;
}
const RadiusDimension* findRadius(const ViewAnnotations& a, const std::string& id) {
    for (const RadiusDimension& d : a.radiusDimensions)
        if (d.id == id) return &d;
    return nullptr;
}
std::vector<std::string> dimIds(const ViewAnnotations& a) {
    std::vector<std::string> ids;
    for (const Dimension& d : a.dimensions) ids.push_back(d.id);
    return ids;
}

const GeneratedView& partView(const DrawingResult& r) { return r.views.front(); }

DrawingResult drawPart(const domain::Model& m, const std::string& id, PartViewFace face = PartViewFace::A) {
    PartDrawingOptions o;
    o.sheet.title.date = "2026-11-16";
    o.view.face = face;
    auto r = generatePartDrawing(m, domain::PartId("cab1/" + id), o);
    EXPECT_TRUE(r.ok()) << (r.ok() ? "" : r.error().message);
    return r.ok() ? std::move(r).value() : DrawingResult{};
}

/// Arc segments "M x1 y1 A r r 0 large sweep x2 y2" of every <path> with data-source="<src>".
struct SvgArc {
    double x1, y1, r, large, sweep, x2, y2;
};
std::vector<SvgArc> svgArcs(const std::string& svg, const std::string& source) {
    std::vector<SvgArc> out;
    const std::string marker = "data-source=\"" + source + "\"";
    for (std::size_t pos = svg.find("<path d=\""); pos != std::string::npos; pos = svg.find("<path d=\"", pos + 1)) {
        const std::size_t end = svg.find('>', pos);
        const std::string tag = svg.substr(pos, end - pos);
        if (tag.find(marker) == std::string::npos) continue;
        std::string d = tag.substr(9, tag.find('"', 9) - 9); // "M x,y A r r 0 l s x,y"
        for (char& ch : d)
            if (ch == ',') ch = ' ';
        std::istringstream in(d);
        std::string m, a;
        SvgArc s{};
        double rot = 0, ry = 0;
        in >> m >> s.x1 >> s.y1 >> a >> s.r >> ry >> rot >> s.large >> s.sweep >> s.x2 >> s.y2;
        if (m == "M" && a == "A") out.push_back(s);
    }
    return out;
}

using Pairs = std::vector<std::pair<int, std::string>>;
Pairs parseDxf(const std::string& dxf) {
    std::vector<std::string> lines;
    std::istringstream in(dxf);
    for (std::string l; std::getline(in, l);) lines.push_back(l);
    Pairs p;
    for (std::size_t i = 0; i + 1 < lines.size(); i += 2) p.emplace_back(std::atoi(lines[i].c_str()), lines[i + 1]);
    return p;
}
struct DxfArc {
    std::string layer;
    double cx = 0, cy = 0, r = 0, a0 = 0, a1 = 0;
};
std::vector<DxfArc> dxfArcs(const std::string& dxf) {
    const Pairs p = parseDxf(dxf);
    std::vector<DxfArc> out;
    for (std::size_t i = 0; i < p.size(); ++i) {
        if (p[i].first != 0 || p[i].second != "ARC") continue;
        DxfArc a;
        for (std::size_t j = i + 1; j < p.size() && p[j].first != 0; ++j) {
            const double v = std::strtod(p[j].second.c_str(), nullptr);
            switch (p[j].first) {
            case 8: a.layer = p[j].second; break;
            case 10: a.cx = v; break;
            case 20: a.cy = v; break;
            case 40: a.r = v; break;
            case 50: a.a0 = v; break;
            case 51: a.a1 = v; break;
            default: break;
            }
        }
        out.push_back(a);
    }
    return out;
}

} // namespace

TEST(ShapedPartView, RoundedShelfOutlineHasFourExactArcs) {
    const domain::Part shelf = roundedShelf();
    const View v = generatePartView(shelf);
    EXPECT_TRUE(v.diagnostics.empty());
    EXPECT_EQ(v.bounds.min, (Vec2{0, 0}));
    EXPECT_EQ(v.bounds.max, (Vec2{600, 400}));
    // 4 straight pieces between the tangent points, 4 quarter arcs R 50.
    ASSERT_EQ(v.segments.size(), 4u);
    EXPECT_TRUE(findSegment(v, {50, 0}, {550, 0}));
    EXPECT_TRUE(findSegment(v, {600, 50}, {600, 350}));
    EXPECT_TRUE(findSegment(v, {550, 400}, {50, 400}));
    EXPECT_TRUE(findSegment(v, {0, 350}, {0, 50}));
    EXPECT_FALSE(findSegment(v, {0, 0}, {600, 0})); // no box outline
    ASSERT_EQ(v.arcs.size(), 4u);
    // Corner centres and counter-clockwise angle ranges (face A: view = part frame):
    //   (550,50): 270 -> 0, (550,350): 0 -> 90, (50,350): 90 -> 180, (50,50): 180 -> 270.
    const struct {
        Vec2 c;
        double start;
    } expected[4] = {{{550, 50}, 270}, {{550, 350}, 0}, {{50, 350}, 90}, {{50, 50}, 180}};
    for (const auto& e : expected) {
        const ViewArc* a = findArc(v, e.c);
        ASSERT_TRUE(a) << e.c.x << "," << e.c.y;
        EXPECT_NEAR(a->radius, 50, 1e-9);
        EXPECT_NEAR(a->startDeg, e.start, 1e-9);
        EXPECT_NEAR(sweepOf(*a), 90, 1e-9);
        EXPECT_EQ(a->style, LineStyle::Visible);
        EXPECT_EQ(a->sourceId, "cab1/shelf.round");
    }
    // Face B mirrors v = -y: centre (550,50) -> (550,-50); the arc from (550,0) to (600,50)
    // becomes (550,0) -> (600,-50), counter-clockwise in view axes from 0 to 90 degrees.
    domain::Part s2 = shelf;
    PartViewOptions ob;
    ob.face = PartViewFace::B;
    const View vb = generatePartView(s2, ob);
    const ViewArc* b1 = findArc(vb, {550, -50});
    ASSERT_TRUE(b1);
    EXPECT_NEAR(b1->startDeg, 0, 1e-9);
    EXPECT_NEAR(sweepOf(*b1), 90, 1e-9);
}

TEST(ShapedPartView, RoundedShelfAnnotations) {
    const domain::Model m = modelWithShapedParts();
    const DrawingResult r = drawPart(m, "shelf.round");
    ASSERT_FALSE(r.views.empty());
    const ViewAnnotations& a = partView(r).annotations;
    const std::string vid = "cab1/shelf.round/view.faceA";
    // Four radius dimensions, one per arc (segments 1, 3, 5, 7), text from the geometry.
    ASSERT_EQ(a.radiusDimensions.size(), 4u);
    for (const char* seg : {"1", "3", "5", "7"}) {
        const RadiusDimension* d = findRadius(a, vid + "/dim.radius[" + seg + "]");
        ASSERT_TRUE(d) << seg;
        EXPECT_FALSE(d->diameter);
        EXPECT_NEAR(measuredValue(*d), 50, 1e-9);
        EXPECT_EQ(dimensionText(*d), "R 50");
    }
    // Leader of segment 1 meets the arc at mid-angle 315: (550 + 50 cos 315, 50 + 50 sin 315)
    //   = (585.3553, 14.6447).
    const RadiusDimension* d1 = findRadius(a, vid + "/dim.radius[1]");
    EXPECT_TRUE(samePoint(d1->center, {550, 50}));
    EXPECT_TRUE(samePoint(d1->arcPoint, {550 + 50 * std::sqrt(0.5), 50 - 50 * std::sqrt(0.5)}, 1e-9));
    // Only the overall dimensions (no holes, no cut-outs).
    EXPECT_EQ(dimIds(a), (std::vector<std::string>{vid + "/dim.overall.length", vid + "/dim.overall.width"}));
    // 8 banded segments -> 8 marks, the 4 arc marks are concentric and convex.
    ASSERT_EQ(a.edgeBandMarks.size(), 8u);
    int arcMarks = 0;
    for (const EdgeBandMark& mk : a.edgeBandMarks) {
        EXPECT_EQ(mk.code, "E1");
        if (!mk.arc) continue;
        ++arcMarks;
        EXPECT_NEAR(mk.arc->radius, 50, 1e-9);
        EXPECT_TRUE(mk.arc->centerInside);
    }
    EXPECT_EQ(arcMarks, 4);
    // Bottom line mark: inward = +y.
    EXPECT_EQ(a.edgeBandMarks[0].id, vid + "/edge.seg[0]");
    EXPECT_TRUE(samePoint(a.edgeBandMarks[0].inward, {0, 1}));
    // Legend: banded length along the finished outline = 2 x 500 + 2 x 300 + 4 x (pi 50 / 2)
    //   = 1600 + 314.159 = 1914.2 mm.
    const Note* legend = findNote(a, vid + "/legend.edge[eb.abs2]");
    ASSERT_TRUE(legend);
    EXPECT_NE(legend->text.find(": 8 outline segments, 1914.2 mm"), std::string::npos) << legend->text;
    // Cut size: every segment offset 2 mm inwards -> bounding box 596 x 396; routed shape noted.
    const Note* cut = findNote(a, vid + "/note.cutsize");
    ASSERT_TRUE(cut);
    EXPECT_EQ(cut->text, "Cut size: 596 \xC3\x97 396 mm, shaped part (routed)");
}

TEST(ShapedPartView, RenderedSheetHasArcPrimitivesAndRadiusTexts) {
    const domain::Model m = modelWithShapedParts();
    const DrawingResult r = drawPart(m, "shelf.round");
    ASSERT_FALSE(r.sheets.empty());
    const Sheet& sheet = r.sheets.front();
    const double sc = sheet.views.front().placement.scale;
    int outlineArcs = 0, markArcs = 0, radiusTexts = 0;
    for (const Primitive& p : sheet.primitives) {
        if (const auto* a = std::get_if<Arc>(&p)) {
            if (a->sourceId == "cab1/shelf.round") {
                ++outlineArcs;
                EXPECT_NEAR(a->radius, 50 * sc, 1e-9);
            }
            if (a->sourceId == "eb.abs2") {
                ++markArcs;
                // Edge mark 1.2 paper mm inside the convex outline.
                EXPECT_NEAR(a->radius, 50 * sc - 1.2, 1e-9);
            }
        }
        if (const auto* t = std::get_if<Text>(&p))
            if (t->text == "R 50") ++radiusTexts;
    }
    EXPECT_EQ(outlineArcs, 4);
    EXPECT_EQ(markArcs, 4);
    EXPECT_EQ(radiusTexts, 4);
}

TEST(ShapedPartView, SvgWritesTrueArcCommands) {
    const domain::Model m = modelWithShapedParts();
    const DrawingResult r = drawPart(m, "shelf.round");
    const Sheet& sheet = r.sheets.front();
    const ViewPlacement pl = sheet.views.front().placement;
    const std::string svg = toSvg(sheet);
    const std::vector<SvgArc> arcs = svgArcs(svg, "cab1/shelf.round");
    ASSERT_EQ(arcs.size(), 4u);
    // Corner arc around (550, 50): CCW from (550, 0) to (600, 50) in view coordinates.
    // Paper p = origin + view x scale; SVG y = sheet height - paper y; 90 degrees -> large-arc 0;
    // counter-clockwise on paper -> sweep-flag 0 after the Y flip.
    const Vec2 p1 = pl.toPaper({550, 0}), p2 = pl.toPaper({600, 50});
    bool found = false;
    for (const SvgArc& a : arcs) {
        if (std::fabs(a.x1 - p1.x) > 1e-3 || std::fabs(a.y1 - (sheet.height - p1.y)) > 1e-3) continue;
        found = true;
        EXPECT_NEAR(a.r, 50 * pl.scale, 1e-4);
        EXPECT_EQ(a.large, 0);
        EXPECT_EQ(a.sweep, 0);
        EXPECT_NEAR(a.x2, p2.x, 1e-3);
        EXPECT_NEAR(a.y2, sheet.height - p2.y, 1e-3);
    }
    EXPECT_TRUE(found);
    EXPECT_NE(svg.find(">R 50</text>"), std::string::npos);
}

TEST(ShapedPartView, DxfWritesArcEntitiesWithExactCentreRadiusAngles) {
    const domain::Model m = modelWithShapedParts();
    const DrawingResult r = drawPart(m, "shelf.round");
    const GeneratedView& gv = partView(r);
    std::vector<DxfArc> outline;
    for (const DxfArc& a : dxfArcs(viewToDxf(gv.view, gv.annotations)))
        if (a.layer == "VISIBLE" && std::fabs(a.r - 50) < 1e-9) outline.push_back(a);
    ASSERT_EQ(outline.size(), 4u); // the edge-mark arcs have radius 50 - 12 = 38 at annotation scale 10
    // Model space 1:1: centres (550,50) 270->0, (550,350) 0->90, (50,350) 90->180, (50,50) 180->270.
    const double expected[4][4] = {{550, 50, 270, 0}, {550, 350, 0, 90}, {50, 350, 90, 180}, {50, 50, 180, 270}};
    for (const auto& e : expected) {
        bool found = false;
        for (const DxfArc& a : outline) {
            if (std::fabs(a.cx - e[0]) > 1e-9 || std::fabs(a.cy - e[1]) > 1e-9) continue;
            found = true;
            EXPECT_NEAR(a.a0, e[2], 1e-9);
            EXPECT_NEAR(a.a1, e[3], 1e-9);
        }
        EXPECT_TRUE(found) << e[0] << "," << e[1];
    }
}

TEST(ShapedPartView, WorktopCutoutsDimensionedAndDrawnSolid) {
    const domain::Model m = modelWithShapedParts();
    const DrawingResult r = drawPart(m, "worktop");
    const View& v = partView(r).view;
    const ViewAnnotations& a = partView(r).annotations;
    const std::string vid = "cab1/worktop/view.faceA";
    // Outline 4 lines + sink 4 lines (all Visible) + 2 centre lines of the tap hole.
    EXPECT_EQ(countStyle(v, LineStyle::Visible), 8u);
    EXPECT_EQ(countStyle(v, LineStyle::Center), 2u);
    EXPECT_TRUE(findSegment(v, {350, 100}, {850, 100}));
    EXPECT_TRUE(findSegment(v, {850, 500}, {350, 500}));
    EXPECT_TRUE(v.arcs.empty());
    ASSERT_EQ(v.circles.size(), 1u);
    EXPECT_EQ(v.circles[0].center, (Vec2{600, 540}));
    EXPECT_DOUBLE_EQ(v.circles[0].radius, 17.5);
    EXPECT_EQ(v.circles[0].style, LineStyle::Visible); // through opening
    // Ordinate dimensions from the part origin, sorted by value, then the overall size:
    //   x: sink min 350, tap centre 600, sink max 850, overall 1200
    //   y: sink min 100, sink max 500, tap centre 540, overall 600
    const std::vector<std::string> ids = dimIds(a);
    ASSERT_EQ(ids.size(), 8u);
    EXPECT_EQ(ids[0], vid + "/dim.cutout.x[0.min]");
    EXPECT_EQ(ids[1], vid + "/dim.cutout.x[1]");
    EXPECT_EQ(ids[2], vid + "/dim.cutout.x[0.max]");
    EXPECT_EQ(ids[3], vid + "/dim.overall.length");
    EXPECT_EQ(ids[4], vid + "/dim.cutout.y[0.min]");
    EXPECT_EQ(ids[5], vid + "/dim.cutout.y[0.max]");
    EXPECT_EQ(ids[6], vid + "/dim.cutout.y[1]");
    EXPECT_EQ(ids[7], vid + "/dim.overall.width");
    const double values[8] = {350, 600, 850, 1200, 100, 500, 540, 600};
    for (std::size_t i = 0; i < 8; ++i) EXPECT_NEAR(measuredValue(a.dimensions[i]), values[i], 1e-9) << ids[i];
    // Sink size follows from the ordinates: 850 - 350 = 500, 500 - 100 = 400.
    // Tap hole: diameter dimension 2 x 17.5 = 35.
    ASSERT_EQ(a.radiusDimensions.size(), 1u);
    EXPECT_EQ(a.radiusDimensions[0].id, vid + "/dim.diameter.cutout[1]");
    EXPECT_TRUE(a.radiusDimensions[0].diameter);
    EXPECT_NEAR(measuredValue(a.radiusDimensions[0]), 35, 1e-9);
    EXPECT_EQ(dimensionText(a.radiusDimensions[0]), "\xC3\x98 35");
    const Note* cut = findNote(a, vid + "/note.cutsize");
    ASSERT_TRUE(cut);
    EXPECT_EQ(cut->text, "Cut size: 1200 \xC3\x97 600 mm, shaped part (routed)");
}

TEST(ShapedPartView, ArcTopSideRadiusFromGeometry) {
    const domain::Model m = modelWithShapedParts();
    const DrawingResult r = drawPart(m, "side.arc");
    const View& v = partView(r).view;
    const ViewAnnotations& a = partView(r).annotations;
    const std::string vid = "cab1/side.arc/view.faceA";
    ASSERT_EQ(v.segments.size(), 3u);
    ASSERT_EQ(v.arcs.size(), 1u);
    // Centre (400, -150), R 850; from (800,600): atan2(750, 400) = 61.9275 deg to
    // (0,600): 180 - 61.9275 = 118.0725 deg; sweep = 4 atan(0.25) = 56.1450 deg.
    const ViewArc& arc = v.arcs[0];
    EXPECT_TRUE(samePoint(arc.center, {400, -150}, 1e-9));
    EXPECT_NEAR(arc.radius, 850, 1e-9);
    const double a0 = core::radToDeg(std::atan2(750.0, 400.0));
    EXPECT_NEAR(arc.startDeg, a0, 1e-9);
    EXPECT_NEAR(arc.endDeg, 180 - a0, 1e-9);
    EXPECT_NEAR(sweepOf(arc), core::radToDeg(4 * std::atan(0.25)), 1e-9);
    EXPECT_EQ(v.bounds.max, (Vec2{800, 700}));
    ASSERT_EQ(a.radiusDimensions.size(), 1u);
    const RadiusDimension& d = a.radiusDimensions[0];
    EXPECT_EQ(d.id, vid + "/dim.radius[2]");
    EXPECT_EQ(dimensionText(d), "R 850");
    // Mid-angle 90 degrees: the leader meets the top of the arc at (400, -150 + 850) = (400, 700).
    EXPECT_TRUE(samePoint(d.arcPoint, {400, 700}, 1e-9));
    // Overall width = bounding box height 700.
    const Dimension* w = findDimension(a.dimensions, vid + "/dim.overall.width");
    ASSERT_TRUE(w);
    EXPECT_NEAR(measuredValue(*w), 700, 1e-9);
    // Face B (v = -y): centre (400, 150); the arc is mirrored, CCW in view axes from
    // 360 - 118.0725 = 241.9275 to 360 - 61.9275 = 298.0725 degrees.
    const DrawingResult rb = drawPart(m, "side.arc", PartViewFace::B);
    const ViewArc& b = partView(rb).view.arcs.at(0);
    EXPECT_TRUE(samePoint(b.center, {400, 150}, 1e-9));
    EXPECT_NEAR(b.startDeg, 180 + a0, 1e-9);
    EXPECT_NEAR(b.endDeg, 360 - a0, 1e-9);
    EXPECT_TRUE(samePoint(partView(rb).annotations.radiusDimensions.at(0).arcPoint, {400, -700}, 1e-9));
}

TEST(ShapedPartView, RadiusDimensionEditsAndStaleOverride) {
    const domain::Model m = modelWithShapedParts();
    const std::string id = "cab1/shelf.round/view.faceA/dim.radius[3]";
    DrawingEdits edits;
    edits.elements["cab1/shelf.round/view.faceA/dim.radius[1]"].hidden = true;
    ElementEdit& e = edits.elements[id];
    e.textOverride = "R 50 (4x)";
    e.measuredAtEdit = 40.0; // made when the radius was 40 -> stale now
    e.offset = 15.0;
    PartDrawingOptions o;
    const auto r = generatePartDrawing(m, domain::PartId("cab1/shelf.round"), o, edits);
    ASSERT_TRUE(r.ok());
    const ViewAnnotations& a = r.value().views.front().annotations;
    const RadiusDimension* d3 = findRadius(a, id);
    ASSERT_TRUE(d3);
    EXPECT_TRUE(d3->staleOverride);
    EXPECT_DOUBLE_EQ(d3->leaderLength, 15.0);
    EXPECT_EQ(dimensionText(*d3), "R 50 (4x) *");
    EXPECT_TRUE(findRadius(a, "cab1/shelf.round/view.faceA/dim.radius[1]")->hidden);
    EXPECT_TRUE(r.value().edits.orphanIds.empty());
    bool stale = false;
    for (const auto& dg : r.value().diagnostics) stale = stale || dg.code == "drawings.edits.stale_override";
    EXPECT_TRUE(stale);
    int texts = 0;
    for (const Primitive& p : r.value().sheets.front().primitives)
        if (const auto* t = std::get_if<Text>(&p)) texts += t->text == "R 50" ? 1 : 0;
    EXPECT_EQ(texts, 2); // 4 radii - 1 hidden - 1 overridden
}

TEST(ShapedPartView, InvalidShapeIsReportedNotCrashed) {
    domain::PartShape s;
    // Bow-tie: self-intersecting.
    s.outer.vertices = {{{0, 0}, 0}, {{100, 100}, 0}, {{100, 0}, 0}, {{0, 100}, 0}};
    const domain::Part p = shapedPart("bad", "Bad", 100, 100, 18, s);
    const View v = generatePartView(p);
    ASSERT_FALSE(v.diagnostics.empty());
    EXPECT_EQ(v.diagnostics[0].code, "drawings.part.invalid_shape");
    EXPECT_EQ(v.segments.size(), 4u);
}
