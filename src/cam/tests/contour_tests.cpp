// SPDX-License-Identifier: AGPL-3.0-only
// Contour routing of shaped parts (ADR-0008): offsets, arcs, simulation, G2/G3 and round trip.
#include "cam_test_fixtures.hpp"

#include "fos/cam/contour.hpp"
#include "fos/cam/generate.hpp"
#include "fos/cam/generic_iso.hpp"
#include "fos/cam/postprocessor.hpp"
#include "fos/cam/roundtrip.hpp"
#include "fos/cam/simulate.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <set>

using namespace fos;
using namespace fos::cam;
using domain::Face;
using geometry::Vec2;
using geometry::Vec3;

namespace {

domain::Part shaped(std::string id, double l, double w, geometry::Contour outer,
                    std::vector<geometry::Contour> cutouts = {}) {
    domain::Part p;
    p.id = domain::PartId(id);
    p.name = id;
    p.length = l;
    p.width = w;
    p.thickness = 18;
    p.material = domain::MaterialId("mat.pb18");
    p.shape = domain::PartShape{std::move(outer), std::move(cutouts), {}};
    return p;
}
domain::Part roundedPanel() {
    return shaped("rounded", 600, 400, geometry::roundedRectangleContour(600, 400, {50, 50, 50, 50}).value());
}
domain::Part archedSide() {
    geometry::Contour c;
    c.vertices = {{{0, 0}, 0}, {{800, 0}, 0}, {{800, 500}, 0.25}, {{0, 500}, 0}};
    return shaped("arched", 800, 600, c);
}
domain::Part worktop() {
    return shaped("worktop", 1200, 600, geometry::rectangleContour(1200, 600),
                  {geometry::translated(geometry::rectangleContour(500, 400), {350, 100}),
                   geometry::circleContour({1000, 300}, 17.5)});
}
/// 1000 x 800 rectangle with a 400 x 300 cut-out with R50 corners at (300, 250).
domain::Part panelWithRoundedCutout() {
    return shaped("cutout", 1000, 800, geometry::rectangleContour(1000, 800),
                  {geometry::translated(geometry::roundedRectangleContour(400, 300, {50, 50, 50, 50}).value(),
                                        {300, 250})});
}

/// Test tools plus T5: 8 mm centre-cutting end mill, flute 30, depth of cut 6 (fictional numbers).
ToolLibrary routerTools() {
    ToolLibrary lib = test::testTools();
    if (!lib.add(test::endMill("em.8", 5, 8, 30, 6))) throw std::logic_error("em.8 rejected");
    return lib;
}

// Blank = part bounding box + 20 mm per side, blank corner at machine (50, 50, 0): the part
// origin is at machine (70, 70, 0).
const Setup kSetupA{Face::A, {50, 50, 0}, 20};

CamResult generate(const domain::Part& part, const Setup& s = kSetupA, CamOptions o = {}) {
    auto r = generateProgram(part, s, test::testProfile(), routerTools(), o);
    if (!r) throw std::logic_error(r.error().message);
    return r.value();
}

double radius(Vec3 start, const Motion& m) { return geometry::length(Vec2{start.x, start.y} - m.centre); }

/// (start position, motion) for every arc of `source`.
std::vector<std::pair<Vec3, Motion>> arcsOf(const Program& p, const std::string& source) {
    std::vector<std::pair<Vec3, Motion>> out;
    Vec3 pos;
    for (const auto& m : p.motions) {
        if (isArc(m.kind) && m.source.str() == source) out.emplace_back(pos, m);
        if ((m.kind == MotionKind::Rapid && !m.zOnly) || isFeedMove(m.kind)) pos = m.target;
        else if (m.zOnly) pos.z = m.target.z;
    }
    return out;
}

bool hasCode(const core::Diagnostics& ds, const std::string& code) {
    for (const auto& d : ds)
        if (d.code == code) return true;
    return false;
}
std::string codes(const core::Diagnostics& ds) {
    std::string s;
    for (const auto& d : ds) s += d.code + ": " + d.message + "\n";
    return s;
}

SimulationReport sim(const Program& p, const domain::Part& part, const Setup& s = kSetupA) {
    return simulate(p, part, s, test::testProfile(), routerTools());
}

/// Replaces every arc of `source` with radius `from` (+-1e-6) by a concentric arc of radius
/// `to`, moving its start point (the previous motion's target) and its end point radially.
Program corruptRadius(Program p, const std::string& source, double from, double to) {
    Vec3 pos;
    for (std::size_t i = 0; i < p.motions.size(); ++i) {
        Motion& m = p.motions[i];
        if (isArc(m.kind) && m.source.str() == source && std::fabs(radius(pos, m) - from) < 1e-6) {
            auto scale = [&](Vec3 q) {
                const Vec2 d = Vec2{q.x, q.y} - m.centre;
                const Vec2 n = m.centre + d * (to / from);
                return Vec3{n.x, n.y, q.z};
            };
            p.motions[i - 1].target = scale(p.motions[i - 1].target);
            m.target = scale(m.target);
        }
        if ((m.kind == MotionKind::Rapid && !m.zOnly) || isFeedMove(m.kind)) pos = m.target;
    }
    return p;
}

} // namespace

// R50 convex outline corner, 8 mm cutter: tool-centre arc radius 50 + 4 = 54, concentric with the
// corner. Corner centres in the part frame (50,50), (550,50), (550,350), (50,350) -> machine +70.
TEST(CamContour, OutlineCornerOffsetRadiusIs54) {
    const CamResult r = generate(roundedPanel());
    EXPECT_EQ(r.toolsUsed, std::vector<int>{5}); // largest end mill whose flute covers 18.5 mm
    const auto arcs = arcsOf(r.program, "contour.outline");
    std::set<std::pair<long, long>> centres;
    std::size_t corner = 0;
    for (const auto& [start, m] : arcs) {
        const double rad = radius(start, m);
        if (std::fabs(rad - 4.0) < 1e-9) continue; // lead-in / lead-out (radius = tool radius)
        EXPECT_NEAR(rad, 54.0, 1e-9);
        EXPECT_NEAR(geometry::length(Vec2{m.target.x, m.target.y} - m.centre), 54.0, 1e-9);
        EXPECT_EQ(m.kind, MotionKind::ArcCW); // climb: outline clockwise
        centres.insert({std::lround(m.centre.x), std::lround(m.centre.y)});
        ++corner;
    }
    // 4 corners x 4 levels (depth 18 + 0.5 breakthrough = 18.5, DOC 6: Z 12, 6, 0, -0.5).
    EXPECT_EQ(corner, 16u);
    EXPECT_EQ(centres, (std::set<std::pair<long, long>>{{120, 120}, {620, 120}, {620, 420}, {120, 420}}));
    std::set<long> levels;
    for (const auto& m : r.program.motions)
        if (isFeedMove(m.kind) && m.source.str() == "contour.outline") levels.insert(std::lround(m.target.z * 10));
    EXPECT_EQ(levels, (std::set<long>{-5, 0, 60, 120}));
    EXPECT_FALSE(hasCode(r.diagnostics, "cam.contour.rounded_corner"));
}

// R50 corner of a cut-out (concave for the part), 8 mm cutter inside: 50 - 4 = 46.
TEST(CamContour, CutoutCornerOffsetRadiusIs46) {
    const CamResult r = generate(panelWithRoundedCutout());
    std::size_t corner = 0;
    for (const auto& [start, m] : arcsOf(r.program, "contour.cutout.0")) {
        const double rad = radius(start, m);
        if (std::fabs(rad - 4.0) < 1e-9) continue;
        EXPECT_NEAR(rad, 46.0, 1e-9);
        EXPECT_EQ(m.kind, MotionKind::ArcCCW); // climb: cut-outs counter-clockwise
        ++corner;
    }
    EXPECT_EQ(corner, 16u);
    // Cut-out before outline.
    std::size_t firstCut = 0, firstOutline = 0;
    for (std::size_t i = r.program.motions.size(); i-- > 0;) {
        if (r.program.motions[i].source.str() == "contour.cutout.0") firstCut = i;
        if (r.program.motions[i].source.str() == "contour.outline") firstOutline = i;
    }
    EXPECT_LT(firstCut, firstOutline);
    EXPECT_FALSE(sim(r.program, panelWithRoundedCutout()).hasErrors());
}

TEST(CamContour, ConventionalReversesDirections) {
    CamOptions o;
    o.contourDirection = ContourDirection::Conventional;
    const CamResult r = generate(panelWithRoundedCutout(), kSetupA, o);
    for (const auto& [start, m] : arcsOf(r.program, "contour.cutout.0"))
        if (std::fabs(radius(start, m) - 46.0) < 1e-9) {
            EXPECT_EQ(m.kind, MotionKind::ArcCW);
        }
    // Conventional: outline counter-clockwise (corner arcs R54); the lead arcs (R4) turn clockwise.
    for (const auto& [start, m] : arcsOf(r.program, "contour.outline"))
        EXPECT_EQ(m.kind, std::fabs(radius(start, m) - 4.0) < 1e-9 ? MotionKind::ArcCW : MotionKind::ArcCCW);
    EXPECT_FALSE(sim(r.program, panelWithRoundedCutout()).hasErrors());
}

TEST(CamContour, Phase12PartsSimulateCleanAndPostprocess) {
    const auto post = builtinPostprocessors();
    for (const domain::Part& part : {roundedPanel(), archedSide(), worktop()}) {
        for (Face f : {Face::A, Face::B}) {
            const cam::Setup s{f, {50, 50, 0}, 20};
            const CamResult r = generate(part, s);
            const SimulationReport rep = sim(r.program, part, s);
            EXPECT_FALSE(rep.hasErrors()) << part.name << " face " << domain::toString(f) << "\n"
                                          << codes(rep.diagnostics);
            auto out = post.find("generic-iso")->generate(
                PostInput{r.program, part, s, test::testProfile(), routerTools()});
            ASSERT_TRUE(out.ok()) << out.error().message;
            EXPECT_NE(out.value().text.find("NOT VERIFIED"), std::string::npos);
            EXPECT_TRUE(out.value().text.find("G2 ") != std::string::npos ||
                        out.value().text.find("G3 ") != std::string::npos);
            EXPECT_FALSE(out.value().verifiedForMachine);
            auto rt = checkRoundTrip(r.program, out.value().text);
            ASSERT_TRUE(rt.ok()) << rt.error().message;
            EXPECT_TRUE(rt.value().matches) << (rt.value().differences.empty() ? "" : rt.value().differences[0]);
        }
    }
}

// Worktop: the sharp sink corners stay rounded R4 (warning); the R17.5 tap hole path is R13.5.
TEST(CamContour, WorktopCornersWarnAndTapHoleRadius) {
    const CamResult r = generate(worktop());
    std::size_t warn = 0;
    for (const auto& d : r.diagnostics)
        if (d.code == "cam.contour.rounded_corner") {
            ++warn;
            EXPECT_NE(d.message.find("4 sharp corner(s)"), std::string::npos) << d.message;
            EXPECT_NE(d.message.find("contour.cutout.0"), std::string::npos) << d.message;
        }
    EXPECT_EQ(warn, 1u);
    std::size_t n = 0;
    for (const auto& [start, m] : arcsOf(r.program, "contour.cutout.1"))
        if (std::fabs(radius(start, m) - 4.0) > 1e-9) {
            EXPECT_NEAR(radius(start, m), 13.5, 1e-9);
            EXPECT_NEAR(m.centre.x, 1070.0, 1e-9); // 1000 + 70
            EXPECT_NEAR(m.centre.y, 370.0, 1e-9);
            ++n;
        }
    EXPECT_GT(n, 0u);
}

// G2 at the top-right corner of the rounded panel, first level. Climb runs the outline clockwise,
// so the top edge is followed in +X to (620, 474), then the R54 corner around (620, 420) to
// (674, 420): I = 620 - 620 = 0, J = 420 - 474 = -54. Z = 18 - 6 = 12, F = em.8 feed 3000.
TEST(CamContour, G2LineForTheTopRightCorner) {
    const domain::Part part = roundedPanel();
    const CamResult r = generate(part);
    auto out = GenericIsoPostprocessor().generate(PostInput{r.program, part, kSetupA, test::testProfile(), routerTools()});
    ASSERT_TRUE(out.ok()) << out.error().message;
    const std::string& t = out.value().text;
    EXPECT_NE(t.find("G1 X620.000 Y474.000 Z12.000 F3000.0\nG2 X674.000 Y420.000 Z12.000 I0.000 J-54.000 F3000.0\n"),
              std::string::npos);
    EXPECT_NE(t.find("BLANK: PART BOUNDING BOX + 20.000 MM PER SIDE"), std::string::npos);
    bool incremental = false;
    for (const auto& w : out.value().reviewWarnings) incremental = incremental || w.find("INCREMENTAL") != std::string::npos;
    EXPECT_TRUE(incremental);

    // Round trip catches a changed centre word.
    std::string bad = t;
    const auto pos = bad.find("I0.000 J-54.000");
    ASSERT_NE(pos, std::string::npos);
    bad.replace(pos, 15, "I0.000 J-53.000");
    auto rt = checkRoundTrip(r.program, bad);
    ASSERT_TRUE(rt.ok());
    EXPECT_FALSE(rt.value().matches);
    ASSERT_FALSE(rt.value().differences.empty());
    EXPECT_NE(rt.value().differences[0].find("arc centre Y"), std::string::npos) << rt.value().differences[0];
    // I/J outside an arc and arcs without I/J are rejected.
    EXPECT_FALSE(parseGenericIso("G21 G90\nG0 X0 Y0 Z5\nG1 X1 Y0 Z5 I1 F100\nM30\n").ok());
    EXPECT_FALSE(parseGenericIso("G21 G90\nG0 X0 Y0 Z5\nG2 X2 Y0 Z5 F100\nM30\n").ok());
    auto ok = parseGenericIso("G21 G90\nG0 X0 Y0 Z5\nG3 X2 Y0 Z4 I1 J0 F100\nM30\n");
    ASSERT_TRUE(ok.ok());
    ASSERT_EQ(ok.value().size(), 2u);
    EXPECT_EQ(ok.value()[1].kind, MotionKind::ArcCCW);
    EXPECT_DOUBLE_EQ(ok.value()[1].centre.x, 1.0);
    EXPECT_DOUBLE_EQ(ok.value()[1].target.z, 4.0);
}

// Corrupting the R54 corner arcs by 1 mm: R53 cuts into the part (gouge), R55 leaves material on
// the outline (not cut). Both are refused by the postprocessor.
TEST(CamContour, CorruptedArcRadiusIsCaught) {
    const domain::Part part = roundedPanel();
    const CamResult r = generate(part);
    ASSERT_FALSE(sim(r.program, part).hasErrors());
    const Program in = corruptRadius(r.program, "contour.outline", 54, 53);
    const SimulationReport a = sim(in, part);
    EXPECT_TRUE(hasCode(a.diagnostics, "cam.sim.gouge")) << codes(a.diagnostics);
    EXPECT_FALSE(hasCode(a.diagnostics, "cam.sim.arc_invalid"));
    const Program outP = corruptRadius(r.program, "contour.outline", 54, 55);
    const SimulationReport b = sim(outP, part);
    EXPECT_TRUE(hasCode(b.diagnostics, "cam.sim.contour_not_cut")) << codes(b.diagnostics);
    EXPECT_FALSE(hasCode(b.diagnostics, "cam.sim.gouge"));
    EXPECT_FALSE(GenericIsoPostprocessor()
                     .generate(PostInput{in, part, kSetupA, test::testProfile(), routerTools()})
                     .ok());
    // Same for a cut-out corner (R46 -> R47 gouges into the part around the opening).
    const domain::Part cp = panelWithRoundedCutout();
    const CamResult rc = generate(cp);
    const SimulationReport c = sim(corruptRadius(rc.program, "contour.cutout.0", 46, 47), cp);
    EXPECT_TRUE(hasCode(c.diagnostics, "cam.sim.gouge")) << codes(c.diagnostics);
    // An inconsistent arc (centre moved, radii differ) is invalid.
    Program bad = r.program;
    for (auto& m : bad.motions)
        if (isArc(m.kind)) {
            m.centre.x += 0.5;
            break;
        }
    EXPECT_TRUE(hasCode(sim(bad, part).diagnostics, "cam.sim.arc_invalid"));
}

TEST(CamContour, MissingRoutingAndToolProblemsAreReported) {
    const domain::Part part = roundedPanel();
    CamOptions off;
    off.routeContours = false;
    const CamResult r = generate(part, kSetupA, off);
    EXPECT_TRUE(r.program.motions.size() <= 3u); // retract only (no operations)
    const SimulationReport rep = sim(r.program, part);
    EXPECT_TRUE(hasCode(rep.diagnostics, "cam.sim.contour_not_cut")) << codes(rep.diagnostics);
    SimulationOptions so;
    so.expectContours = false;
    EXPECT_FALSE(simulate(r.program, part, kSetupA, test::testProfile(), routerTools(), so).hasErrors());

    // A 6 mm hole cannot be routed by the 8 mm or the 6 mm end mill (path radius <= 0).
    const domain::Part small = shaped("small", 400, 400, geometry::rectangleContour(400, 400),
                                      {geometry::circleContour({200, 200}, 3)});
    auto g = generateProgram(small, kSetupA, test::testProfile(), routerTools());
    ASSERT_FALSE(g.ok());
    EXPECT_NE(g.error().message.find("em.8"), std::string::npos) << g.error().message;
    EXPECT_NE(g.error().message.find("em.6"), std::string::npos) << g.error().message;
    CamOptions t1;
    t1.contourToolNumber = 1; // a drill
    EXPECT_FALSE(generateProgram(part, kSetupA, test::testProfile(), routerTools(), t1).ok());
    // Explicit 6 mm end mill: outline path R53.
    CamOptions t4;
    t4.contourToolNumber = 4;
    auto g4 = generateProgram(part, kSetupA, test::testProfile(), routerTools(), t4);
    ASSERT_TRUE(g4.ok()) << g4.error().message;
    for (const auto& [start, m] : arcsOf(g4.value().program, "contour.outline"))
        if (std::fabs(radius(start, m) - 3.0) > 1e-9) {
            EXPECT_NEAR(radius(start, m), 53.0, 1e-9);
        }
    EXPECT_FALSE(sim(g4.value().program, part).hasErrors());
}

TEST(CamContour, BandedOutlineRoutesTheRawContourAndNeedsTheModel) {
    domain::Part part = roundedPanel();
    part.shape->outerEdges.assign(part.shape->outer.size(), domain::EdgeBandId("eb.abs2"));
    EXPECT_FALSE(generateProgram(part, kSetupA, test::testProfile(), routerTools()).ok());
    const domain::Model model = domain::fixtures::basicCabinet(); // eb.abs2 = 2 mm
    CamOptions o;
    o.model = &model;
    const CamResult r = generate(part, kSetupA, o);
    // Raw corner R50 - 2 = 48, tool path 48 + 4 = 52.
    for (const auto& [start, m] : arcsOf(r.program, "contour.outline"))
        if (std::fabs(radius(start, m) - 4.0) > 1e-9) {
            EXPECT_NEAR(radius(start, m), 52.0, 1e-9);
        }
    SimulationOptions so;
    so.model = &model;
    const SimulationReport rep = simulate(r.program, part, kSetupA, test::testProfile(), routerTools(), so);
    EXPECT_FALSE(rep.hasErrors()) << codes(rep.diagnostics);
    // Without the model the simulator cannot derive the raw outline.
    EXPECT_TRUE(hasCode(sim(r.program, part).diagnostics, "cam.sim.setup_mismatch"));
}

// Face B mapping of a contour from first principles: (x, y) -> (o.x + x, o.y + W - y), a mirror
// image; the result must still be counter-clockwise with the same area.
TEST(CamContour, FaceBContourMappingFromFirstPrinciples) {
    const domain::Part part = archedSide(); // W = 600, arc top reaches part y = 600
    auto frame = makeSetupFrame(part, cam::Setup{Face::B, {50, 50, 0}, 20});
    ASSERT_TRUE(frame.ok());
    const geometry::Contour m = contourToMachineXY(frame.value(), part.shape->outer);
    EXPECT_GT(geometry::signedArea(m), 0);
    EXPECT_NEAR(geometry::signedArea(m), geometry::signedArea(part.shape->outer), 1e-6);
    const auto b = geometry::bounds(m);
    // o = (70, 70): X 70..870; the arc apex (part y 600) maps to Y 70 + 600 - 600 = 70.
    EXPECT_NEAR(b.min.x, 70.0, 1e-9);
    EXPECT_NEAR(b.max.x, 870.0, 1e-9);
    EXPECT_NEAR(b.min.y, 70.0, 1e-9);
    EXPECT_NEAR(b.max.y, 670.0, 1e-9);
    // Part point (400, 590) lies under the arc apex -> machine (470, 80), inside; part point
    // (10, 590) lies above the arc (outside the part) -> machine (80, 80), outside.
    EXPECT_EQ(geometry::locatePoint(m, {470, 80}), geometry::PointLocation::Inside);
    EXPECT_EQ(geometry::locatePoint(m, {80, 80}), geometry::PointLocation::Outside);
}

// Rectangular parts: no contour routing, identical programs, no blank allowance.
TEST(CamContour, RectangularPartsUnchanged) {
    const domain::Part side = test::leftSide();
    const cam::Setup s{Face::B, {10, 20, 0}};
    CamOptions off;
    off.routeContours = false;
    auto a = generateProgram(side, s, test::testProfile(), test::testTools());
    auto b = generateProgram(side, s, test::testProfile(), test::testTools(), off);
    ASSERT_TRUE(a.ok() && b.ok());
    ASSERT_EQ(a.value().program.motions.size(), b.value().program.motions.size());
    for (std::size_t i = 0; i < a.value().program.motions.size(); ++i) {
        EXPECT_EQ(describe(a.value().program.motions[i]), describe(b.value().program.motions[i]));
        EXPECT_FALSE(isArc(a.value().program.motions[i].kind));
    }
    EXPECT_FALSE(needsContourRouting(side));
    EXPECT_FALSE(generateProgram(side, cam::Setup{Face::B, {10, 20, 0}, 5}, test::testProfile(), test::testTools()).ok());
    // A shape that is a plain rectangle is not routed either.
    domain::Part rect = test::panel();
    rect.shape = domain::PartShape{geometry::rectangleContour(600, 400), {}, {}};
    EXPECT_FALSE(needsContourRouting(rect));
}
