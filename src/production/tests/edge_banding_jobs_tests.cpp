// SPDX-License-Identifier: AGPL-3.0-only
/// Edge-banding job list for the banding machine operator. All expectations hand-computed.
#include "fos/production/csv.hpp"
#include "fos/production/edge_banding_jobs.hpp"
#include "test_models.hpp"

#include <gtest/gtest.h>

using namespace fos;
using namespace fos::production;
using production::testing::panel;
using production::testing::smallModel;

namespace {
constexpr double kPi = 3.14159265358979323846;
const domain::EdgeBandId kAbs2("eb2"), kAbs04("eb04"), kGhost("ghost");
using domain::EdgeSide;

bool contains(const std::string& h, const std::string& n) { return h.find(n) != std::string::npos; }

domain::EdgeBand band(const domain::EdgeBandId& id, const std::string& name, double t, double w) {
    domain::EdgeBand b;
    b.id = id;
    b.name = name;
    b.thickness = t;
    b.width = w;
    return b;
}

/// Door 700 x 400 banded all round with ABS 2 (part 1); side 300 x 560 with W1, W2 in ABS 2 and
/// L1 in ABS 0.4 (part 2); raw shelf (part 3, not listed); rounded 600 x 400 R 50 banded all
/// round in ABS 2 (part 4); a panel 500 x 300 whose L1 references a missing band (part 5).
domain::Model model() {
    domain::Model m = smallModel();
    // Model order of the bands: eb04 first, so the groups list eb04 before eb2.
    m.edgeBands = {band(kAbs04, "ABS 0.4", 0.4, 22), band(kAbs2, "ABS 2", 2, 23)};
    domain::Part door = panel("door", "Door", 700, 400, 18, "m1");
    door.edges = {kAbs2, kAbs2, kAbs2, kAbs2};
    domain::Part side = panel("side", "Side", 300, 560, 18, "m1");
    side.edge(EdgeSide::L1) = kAbs04;
    side.edge(EdgeSide::W1) = kAbs2;
    side.edge(EdgeSide::W2) = kAbs2;
    domain::Part shelf = panel("shelf", "Shelf", 500, 300, 18, "m1");
    domain::Part round = panel("round", "Round", 600, 400, 18, "m1");
    const double radii[4] = {50, 50, 50, 50};
    domain::PartShape s;
    s.outer = geometry::roundedRectangleContour(600, 400, radii).value();
    s.outerEdges.assign(s.outer.size(), kAbs2);
    round.shape = s;
    domain::Part odd = panel("odd", "Odd", 500, 300, 18, "m1");
    odd.edge(EdgeSide::L1) = kGhost;
    m.assemblies.front().parts = {door, side, shelf, round, odd};
    return m;
}

const PartBandingJob* findPart(const EdgeBandJobGroup& g, const std::string& id) {
    for (const auto& p : g.parts)
        if (p.partId.str() == id) return &p;
    return nullptr;
}
} // namespace

TEST(EdgeBandingJobs, GroupsOrderAndLengthsWithOverhang) {
    const auto jobs = edgeBandingJobs(model()); // default overhang 25 mm per end
    EXPECT_EQ(jobs.overhangPerEnd, 25.0);
    // Bands: eb04, eb2 (model order), then the missing "ghost".
    ASSERT_EQ(jobs.groups.size(), 3u);
    EXPECT_EQ(jobs.groups[0].edgeBandId, kAbs04);
    EXPECT_EQ(jobs.groups[1].edgeBandId, kAbs2);
    EXPECT_EQ(jobs.groups[2].edgeBandId, kGhost);
    EXPECT_TRUE(jobs.groups[2].missing);

    // ABS 2: door (4 edges) + side (W1, W2) = 6 edges; lengths 2*700 + 2*400 + 2*560 = 3320 mm;
    // strips 3320 + 6 * 2 * 25 = 3620 mm.
    const auto& g = jobs.groups[1];
    EXPECT_EQ(g.thickness, 2.0);
    EXPECT_EQ(g.width, 23.0);
    ASSERT_EQ(g.parts.size(), 2u);
    EXPECT_EQ(g.edgeCount, 6);
    EXPECT_NEAR(g.totalEdgeLength, 3320, 1e-9);
    EXPECT_NEAR(g.totalStripLength, 3620, 1e-9);

    // Door: cut 700 - 2 - 2 = 696 x 400 - 2 - 2 = 396. Long edges first: L1, L2 (700), W1, W2 (400).
    const auto* door = findPart(g, "door");
    ASSERT_NE(door, nullptr);
    EXPECT_EQ(door->partNumber, 1);
    EXPECT_NEAR(door->cutLength, 696, 1e-9);
    EXPECT_NEAR(door->cutWidth, 396, 1e-9);
    EXPECT_FALSE(door->otherBands);
    ASSERT_EQ(door->edges.size(), 4u);
    const EdgeSide doorOrder[4] = {EdgeSide::L1, EdgeSide::L2, EdgeSide::W1, EdgeSide::W2};
    const double doorLen[4] = {700, 700, 400, 400}, doorCut[4] = {696, 696, 396, 396};
    for (int i = 0; i < 4; ++i) {
        EXPECT_EQ(door->edges[static_cast<std::size_t>(i)].side, doorOrder[i]);
        EXPECT_EQ(door->edges[static_cast<std::size_t>(i)].step, i + 1);
        EXPECT_NEAR(door->edges[static_cast<std::size_t>(i)].edgeLength, doorLen[i], 1e-9);
        EXPECT_NEAR(door->edges[static_cast<std::size_t>(i)].cutEdgeLength, doorCut[i], 1e-9);
        EXPECT_NEAR(door->edges[static_cast<std::size_t>(i)].stripLength, doorLen[i] + 50, 1e-9); // 750 / 450
    }

    // Side 300 x 560: the W edges (560) are the long ones -> W1 step 1, W2 step 2, L1 (300) step 3.
    // Cut: 300 - 2 - 2 = 296 long, 560 - 0.4 = 559.6 wide.
    const auto* side = findPart(g, "side");
    ASSERT_NE(side, nullptr);
    EXPECT_TRUE(side->otherBands);
    EXPECT_EQ(side->stepsTotal, 3);
    ASSERT_EQ(side->edges.size(), 2u);
    EXPECT_EQ(side->edges[0].side, EdgeSide::W1);
    EXPECT_EQ(side->edges[0].step, 1);
    EXPECT_EQ(side->edges[1].side, EdgeSide::W2);
    EXPECT_EQ(side->edges[1].step, 2);
    EXPECT_NEAR(side->edges[0].cutEdgeLength, 559.6, 1e-9);
    EXPECT_NEAR(side->edges[0].stripLength, 610, 1e-9); // 560 + 50
    const auto& g04 = jobs.groups[0];
    ASSERT_EQ(g04.parts.size(), 1u);
    ASSERT_EQ(g04.parts[0].edges.size(), 1u);
    EXPECT_EQ(g04.parts[0].edges[0].side, EdgeSide::L1);
    EXPECT_EQ(g04.parts[0].edges[0].step, 3);
    EXPECT_NEAR(g04.parts[0].edges[0].cutEdgeLength, 296, 1e-9);
    EXPECT_NEAR(g04.totalStripLength, 350, 1e-9); // 300 + 50

    // Missing band: thickness unknown (0), so cut 500 x 300; strip 500 + 50.
    EXPECT_NEAR(jobs.groups[2].totalStripLength, 550, 1e-9);
    int missing = 0;
    for (const auto& d : jobs.diagnostics) missing += d.code == "production.edgeband.missing";
    EXPECT_EQ(missing, 1);

    // Shelf without bands is not listed anywhere.
    for (const auto& grp : jobs.groups) EXPECT_EQ(findPart(grp, "shelf"), nullptr);
}

TEST(EdgeBandingJobs, ShapedPartsAreListedForContourBanding) {
    const auto jobs = edgeBandingJobs(model());
    ASSERT_EQ(jobs.contour.size(), 1u);
    const auto& c = jobs.contour[0];
    EXPECT_EQ(c.partId.str(), "round");
    EXPECT_EQ(c.partNumber, 4);
    EXPECT_TRUE(c.routed);
    EXPECT_TRUE(c.lengthsAvailable);
    // Blank 600 - 4 x 400 - 4 (2 mm band all round, domain::cutSize).
    EXPECT_NEAR(c.cutLength, 596, 1e-9);
    EXPECT_NEAR(c.cutWidth, 396, 1e-9);
    // 8 segments: 4 straights (500, 300, 500, 300 in some order) + 4 quarter arcs pi/2 * 50.
    ASSERT_EQ(c.segments.size(), 8u);
    double sum = 0;
    for (const auto& s : c.segments) sum += s.length.value();
    const double perimeter = 2 * (500 + 300) + 2 * kPi * 50; // 1914.159 mm
    EXPECT_NEAR(sum, perimeter, 1e-9);
    // Tangent all round: ONE strip, perimeter + 2 * 25.
    ASSERT_EQ(c.strips.size(), 1u);
    EXPECT_NEAR(c.strips[0].length, perimeter, 1e-9);
    EXPECT_NEAR(c.strips[0].stripLength, perimeter + 50, 1e-9);
    ASSERT_EQ(jobs.contourTotals.size(), 1u);
    EXPECT_EQ(jobs.contourTotals[0].edgeBandId, kAbs2);
    EXPECT_EQ(jobs.contourTotals[0].stripCount, 1);
}

TEST(EdgeBandingJobs, BrokenShapeIsListedWithoutLengths) {
    domain::Model m = model();
    auto& round = m.assemblies.front().parts[3];
    round.shape->outerEdges.resize(3); // 3 entries for 8 segments
    round.edge(EdgeSide::L1) = kAbs2;   // edges on a shaped part are ignored with a warning
    const auto jobs = edgeBandingJobs(m);
    ASSERT_EQ(jobs.contour.size(), 1u);
    EXPECT_FALSE(jobs.contour[0].lengthsAvailable);
    EXPECT_EQ(jobs.contour[0].segments.size(), 3u);
    EXPECT_FALSE(jobs.contour[0].segments[0].length.has_value());
    EXPECT_TRUE(jobs.contour[0].strips.empty());
    int unavailable = 0, ignored = 0;
    for (const auto& d : jobs.diagnostics) {
        unavailable += d.code == "production.edgeband_jobs.shape_lengths_unavailable";
        ignored += d.code == "production.edgeband_jobs.edges_on_shaped_part";
    }
    EXPECT_EQ(unavailable, 1);
    EXPECT_EQ(ignored, 1);
    // The table still carries the part.
    bool listed = false;
    for (const auto& r : edgeBandingJobsTable(jobs).rows) listed = listed || (r[6] == "round" && r[10] == "lengths unavailable");
    EXPECT_TRUE(listed);
}

TEST(EdgeBandingJobs, OverhangSettingScopeAndBadEdges) {
    domain::Model m = model();
    EdgeBandingOptions o;
    o.overhangPerEnd = 10;
    auto jobs = edgeBandingJobs(m, o);
    EXPECT_NEAR(jobs.groups[1].totalStripLength, 3320 + 6 * 20, 1e-9); // 3440
    o.overhangPerEnd = -5; // treated as 0
    jobs = edgeBandingJobs(m, o);
    EXPECT_NEAR(jobs.groups[1].totalStripLength, 3320, 1e-9);

    Scope scope;
    scope.assembly = domain::AssemblyId("nope");
    jobs = edgeBandingJobs(m, {}, scope);
    EXPECT_TRUE(jobs.groups.empty());
    EXPECT_TRUE(jobs.contour.empty());
    ASSERT_EQ(jobs.diagnostics.size(), 1u);
    EXPECT_EQ(jobs.diagnostics[0].code, "production.scope.unknown_assembly");

    // A banded edge of zero length is skipped with a diagnostic, never crashes.
    m.assemblies.front().parts[0].width = 0; // door: W1/W2 have length 0
    jobs = edgeBandingJobs(m);
    int invalid = 0;
    for (const auto& d : jobs.diagnostics) invalid += d.code == "production.edgeband_jobs.invalid_edge";
    EXPECT_EQ(invalid, 2);
    EXPECT_EQ(findPart(jobs.groups[1], "door")->edges.size(), 2u);
}

TEST(EdgeBandingJobs, TextCsvJsonAndSvg) {
    const auto jobs = edgeBandingJobs(model());
    const std::string text = edgeBandingJobsText(jobs);
    EXPECT_TRUE(contains(text, kBandingOrderNote));
    EXPECT_TRUE(contains(text, "Band eb2 'ABS 2' 2 x 23 mm: 2 part(s), 6 edge(s), edge length 3.32 m, "
                               "strip length incl. overhang 3.62 m"));
    EXPECT_TRUE(contains(text, "#1 Door (door)  cut 696 x 396  finished 700 x 400\n"));
    EXPECT_TRUE(contains(text, "step 1/4  L1  cut edge 696  edge 700  strip 750\n"));
    EXPECT_TRUE(contains(text, "step 3/3  L1  cut edge 296  edge 300  strip 350\n"));
    EXPECT_TRUE(contains(text, "Band ghost (MISSING from the model)"));
    EXPECT_TRUE(contains(text, std::string("Shaped parts: ") + kContourBandingNote));
    EXPECT_TRUE(contains(text, "strip 1  eb2  length 1914.2  strip 1964.2"));

    const Table t = edgeBandingJobsTable(jobs);
    EXPECT_EQ(t.header.size(), 15u);
    // 1 (eb04) + 6 (eb2) + 1 (ghost) edges + 1 contour strip.
    ASSERT_EQ(t.rows.size(), 9u);
    EXPECT_EQ(t.rows[0], (std::vector<std::string>{"eb04", "ABS 0.4", "0.4", "22", "edge", "2", "side", "Side", "296",
                                                   "559.6", "L1", "3/3", "296", "300", "350"}));
    EXPECT_EQ(t.rows[8][4], "contour");
    EXPECT_TRUE(contains(toCsv(t), "eb2,ABS 2,2,23,edge,1,door,Door,696,396,L1,1/4,696,700,750"));

    const std::string json = edgeBandingJobsJson(jobs);
    EXPECT_TRUE(contains(json, "\"overhangPerEnd\": 25"));
    EXPECT_TRUE(contains(json, "{\"side\": \"L1\", \"step\": 1, \"cutEdgeLength\": 696, \"edgeLength\": 700, "
                               "\"stripLength\": 750}"));
    EXPECT_TRUE(contains(json, "\"routed\": true"));
    EXPECT_TRUE(contains(json, "\"code\": \"production.edgeband.missing\""));

    const auto sheets = edgeBandingJobSheetsSvg(jobs);
    ASSERT_EQ(sheets.size(), 3u); // eb04, eb2, ghost
    EXPECT_EQ(sheets[1].edgeBandId, kAbs2);
    ASSERT_EQ(sheets[1].pages.size(), 1u);
    const std::string& svg = sheets[1].pages[0];
    EXPECT_TRUE(contains(svg, "viewBox=\"0 0 210 297\""));
    EXPECT_TRUE(contains(svg, "Edge banding - eb2 &apos;ABS 2&apos;") || contains(svg, "Edge banding - eb2 'ABS 2'"));
    EXPECT_TRUE(contains(svg, "1/4 L1: edge 700, strip 750 (cut edge 696)"));
    EXPECT_TRUE(contains(svg, kContourBandingNote));
    EXPECT_TRUE(contains(svg, "1914.2 (strip 1964.2)"));
}

TEST(EdgeBandingJobs, SvgPaginatesLongGroups) {
    domain::Model m = smallModel();
    m.edgeBands = {band(kAbs2, "ABS 2", 2, 23)};
    for (int i = 0; i < 13; ++i) {
        domain::Part p = panel("p" + std::to_string(i), "P & <" + std::to_string(i) + ">", 400, 300, 18, "m1");
        p.edge(EdgeSide::L1) = kAbs2;
        m.assemblies.front().parts.push_back(p);
    }
    // Rows per page: floor((297 - 42 - 14) / 20) = 12 -> 13 parts need 2 pages.
    const auto sheets = edgeBandingJobSheetsSvg(edgeBandingJobs(m));
    ASSERT_EQ(sheets.size(), 1u);
    ASSERT_EQ(sheets[0].pages.size(), 2u);
    EXPECT_TRUE(contains(sheets[0].pages[1], "page 2 / 2"));
    EXPECT_TRUE(contains(sheets[0].pages[0], "P &amp; &lt;0&gt;"));
    EXPECT_FALSE(contains(sheets[0].pages[0], "P & <"));
}
