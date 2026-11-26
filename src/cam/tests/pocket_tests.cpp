// SPDX-License-Identifier: AGPL-3.0-only
#include "cam_test_fixtures.hpp"

#include "fos/cam/generate.hpp"
#include "fos/cam/pocket.hpp"
#include "fos/cam/simulate.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <set>

using namespace fos;
using namespace fos::cam;
using domain::Face;

namespace {

const Setup kSetupA{Face::A, {10, 20, 0}};

/// 40 x 200 x 8 pocket at face (200..240, 100..300): a recessed-grip shape.
domain::Part gripPanel(Face face = Face::A) {
    auto part = test::panel();
    domain::Pocket pk;
    pk.face = face;
    pk.contour = {{200, 100}, {240, 100}, {240, 300}, {200, 300}};
    pk.depth = 8;
    part.operations.push_back(test::op("grip", pk));
    return part;
}

CamOptions opts(double radius = 4) {
    CamOptions o;
    o.pocketCornerRadius = radius;
    return o;
}

SimulationOptions simOpts(double radius = 4) {
    SimulationOptions s;
    s.pocketCornerRadius = radius;
    return s;
}

bool has(const core::Diagnostics& d, const std::string& code) {
    for (const auto& x : d)
        if (x.code == code) return true;
    return false;
}

std::string dump(const core::Diagnostics& d) {
    std::string s;
    for (const auto& x : d) s += x.code + ": " + x.message + "\n";
    return s;
}

} // namespace

// Machine rectangle: X 210..250, Y 120..320 (stock origin (10, 20)), top Z 18, floor Z 10.
// em.6 (r = 3, DOC 3) is the only end mill; 2 x R = 8 >= 6. Finishing region C = pocket shrunk by 3:
// X 213..247, Y 123..317; path corner radius rho0 = 4 - 3 = 1.
TEST(CamPocket, GripPocketStaysInsideAndSimulates) {
    const auto part = gripPanel();
    auto r = generateProgram(part, kSetupA, test::testProfile(), test::testTools(), opts());
    ASSERT_TRUE(r) << r.error().message;
    EXPECT_EQ(r.value().plan.machinableNow.size(), 1u);
    EXPECT_EQ(r.value().toolsUsed, std::vector<int>{4});
    EXPECT_TRUE(has(r.value().diagnostics, "cam.pocket.rounded_corner"));
    const auto& prog = r.value().program;
    for (const auto& m : prog.motions) {
        if (!isFeedMove(m.kind)) continue;
        EXPECT_GE(m.target.x, 213 - 1e-9) << describe(m);
        EXPECT_LE(m.target.x, 247 + 1e-9) << describe(m);
        EXPECT_GE(m.target.y, 123 - 1e-9) << describe(m);
        EXPECT_LE(m.target.y, 317 + 1e-9) << describe(m);
        EXPECT_GE(m.target.z, 10 - 1e-9) << describe(m);
        if (isArc(m.kind)) {
            // Every arc is a corner of a loop: its centre is inside C and its quarter stays in C.
            EXPECT_EQ(m.kind, MotionKind::ArcCCW); // climb milling, spindle CW
            EXPECT_GE(m.centre.x - 1, 213 - 1e-9);
            EXPECT_LE(m.centre.x + 1, 247 + 1e-9);
        }
    }
    const auto sim = simulate(prog, part, kSetupA, test::testProfile(), test::testTools(), simOpts());
    EXPECT_FALSE(sim.hasErrors()) << dump(sim.diagnostics);
    EXPECT_GT(sim.cellsRemoved, 0u);
}

// Depth 8, DOC 3: levels Z 18 - 3 = 15, 18 - 6 = 12, floor 10. Cutting moves only at those levels
// (plus the plunges ending there).
TEST(CamPocket, DepthPerPassAndFinishingWall) {
    const auto part = gripPanel();
    auto r = generateProgram(part, kSetupA, test::testProfile(), test::testTools(), opts());
    ASSERT_TRUE(r) << r.error().message;
    std::set<double> zs;
    int finishArcs = 0, wallLines = 0;
    for (const auto& m : r.value().program.motions) {
        if (!isFeedMove(m.kind)) continue;
        zs.insert(std::round(m.target.z * 1000) / 1000);
        // Finishing loop: corner arcs of radius rho0 = 1 around (246, 124), (246, 316), (214, 316), (214, 124).
        if (isArc(m.kind) && std::fabs(m.centre.x - 246) < 1e-9 && std::fabs(m.centre.y - 124) < 1e-9 &&
            std::fabs(std::hypot(m.target.x - 246, m.target.y - 124) - 1) < 1e-9) {
            ++finishArcs;
            EXPECT_NEAR(m.target.x, 247, 1e-9);
            EXPECT_NEAR(m.target.y, 124, 1e-9); // quarter arc (246, 123) -> (247, 124)
        }
        // Left wall line of the finishing loop at X = 213 from Y 316 down to 124.
        if (m.kind == MotionKind::Linear && std::fabs(m.target.x - 213) < 1e-9 && std::fabs(m.target.y - 124) < 1e-9)
            ++wallLines;
    }
    EXPECT_EQ(zs, (std::set<double>{10, 12, 15}));
    EXPECT_EQ(finishArcs, 3); // one finishing loop per level (roughing corner arcs are concentric, radius <= 0.8)
    EXPECT_EQ(wallLines, 3);
}

// Roughing insets: m = 17 (half of C's 34 mm width), stepover 0.4 x 6 = 2.4, allowance 0.2:
// n = ceil(16.8 / 2.4) = 7 loops at insets 14.6, 12.2, ..., 0.2. Loop starts at (230, 123 + inset).
TEST(CamPocket, InsideOutLoopStarts) {
    const auto part = gripPanel();
    auto r = generateProgram(part, kSetupA, test::testProfile(), test::testTools(), opts());
    ASSERT_TRUE(r) << r.error().message;
    std::vector<double> starts;
    for (const auto& m : r.value().program.motions)
        if (m.kind == MotionKind::Linear && std::fabs(m.target.x - 230) < 1e-9 && std::fabs(m.target.z - 15) < 1e-9 &&
            m.target.y < 140)
            starts.push_back(m.target.y);
    // First level: 7 loop starts (each reached by the link and closed by the loop's last line) + finish entry.
    std::vector<double> expected;
    for (int k = 1; k <= 7; ++k) expected.push_back(123 + 17 - 2.4 * k);
    std::vector<double> got;
    for (double y : starts)
        if (got.empty() || std::fabs(got.back() - y) > 1e-9) got.push_back(y);
    ASSERT_GE(got.size(), 7u);
    for (int k = 0; k < 7; ++k) EXPECT_NEAR(got[static_cast<std::size_t>(k)], expected[static_cast<std::size_t>(k)], 1e-9);
}

TEST(CamPocket, RefusesTooLargeToolNamingNeededDiameter) {
    const auto part = gripPanel();
    ToolLibrary lib;
    ASSERT_TRUE(lib.add(test::endMill("em.10", 5, 10, 25, 4)));
    auto r = generateProgram(part, kSetupA, test::testProfile(), lib, opts());
    ASSERT_FALSE(r);
    EXPECT_NE(r.error().message.find("diameter <= 8 mm"), std::string::npos) << r.error().message;
    EXPECT_NE(r.error().message.find("em.10"), std::string::npos) << r.error().message;
    // Explicit tool choice is checked too.
    CamOptions o = opts();
    o.pocketToolNumber = 5;
    auto r2 = generateProgram(part, kSetupA, test::testProfile(), lib, o);
    ASSERT_FALSE(r2);
    EXPECT_NE(r2.error().message.find("exceeds 8 mm"), std::string::npos) << r2.error().message;
}

TEST(CamPocket, SharpCornersWithoutAcceptedRadiusAreUnsupported) {
    const auto part = gripPanel();
    auto r = generateProgram(part, kSetupA, test::testProfile(), test::testTools());
    ASSERT_TRUE(r) << r.error().message;
    ASSERT_EQ(r.value().plan.unsupported.size(), 1u);
    EXPECT_NE(r.value().plan.unsupported[0].reason.find("pocketCornerRadius"), std::string::npos);
    EXPECT_TRUE(has(r.value().diagnostics, "cam.setup.unsupported"));
    // Through pockets and non-rectangles stay unsupported with a reason.
    auto part2 = test::panel();
    domain::Pocket tri;
    tri.contour = {{100, 100}, {200, 100}, {150, 180}};
    tri.depth = 5;
    part2.operations.push_back(test::op("tri", tri));
    auto plan = planSetup(part2, kSetupA, 4);
    ASSERT_TRUE(plan);
    ASSERT_EQ(plan.value().unsupported.size(), 1u);
    EXPECT_NE(plan.value().unsupported[0].reason.find("axis-parallel rectangle"), std::string::npos);
}

TEST(CamPocket, SimulatorCatchesCornerMismatchAndGouge) {
    const auto part = gripPanel();
    auto r = generateProgram(part, kSetupA, test::testProfile(), test::testTools(), opts());
    ASSERT_TRUE(r) << r.error().message;
    // Expecting R 2 corners: the R 4 corners leave material that should have been removed.
    auto sim = simulate(r.value().program, part, kSetupA, test::testProfile(), test::testTools(), simOpts(2));
    EXPECT_TRUE(has(sim.diagnostics, "cam.sim.pocket_not_cut")) << dump(sim.diagnostics);
    // Moving the last finishing line 1 mm outward (X 213 -> 212) cuts past the wall X 210.
    Program bad = r.value().program;
    for (auto it = bad.motions.rbegin(); it != bad.motions.rend(); ++it)
        if (it->kind == MotionKind::Linear && std::fabs(it->target.x - 213) < 1e-9 && std::fabs(it->target.z - 10) < 1e-9) {
            it->target.x = 212;
            break;
        }
    auto sim2 = simulate(bad, part, kSetupA, test::testProfile(), test::testTools(), simOpts());
    EXPECT_TRUE(has(sim2.diagnostics, "cam.sim.gouge")) << dump(sim2.diagnostics);
}

// Face B up (the sliding-door grip lies on face B): v -> W - v, so face (200..240, 100..300) maps to
// machine X 210..250, Y 20 + 400 - 300 = 120 .. 20 + 400 - 100 = 320.
TEST(CamPocket, FaceBPocketSimulates) {
    const auto part = gripPanel(Face::B);
    const cam::Setup s{Face::B, {10, 20, 0}};
    auto r = generateProgram(part, s, test::testProfile(), test::testTools(), opts());
    ASSERT_TRUE(r) << r.error().message;
    const auto sim = simulate(r.value().program, part, s, test::testProfile(), test::testTools(), simOpts());
    EXPECT_FALSE(sim.hasErrors()) << dump(sim.diagnostics);
}

// Rebate along the front edge: face (100..300, 0..20), depth 6, open at y = 0 (machine Y 20).
// C: X 113..307, Y 20 - (3 + 1) = 16 .. 40 - 3 = 37: the tool runs 4 mm past the stock edge.
TEST(CamPocket, EdgeOpenPocketClearsPastTheEdge) {
    auto part = test::panel();
    domain::Pocket pk;
    pk.contour = {{100, 0}, {300, 0}, {300, 20}, {100, 20}};
    pk.depth = 6;
    part.operations.push_back(test::op("rebate", pk));
    auto r = generateProgram(part, kSetupA, test::testProfile(), test::testTools(), opts());
    ASSERT_TRUE(r) << r.error().message;
    double minY = 1e9;
    for (const auto& m : r.value().program.motions)
        if (isFeedMove(m.kind)) minY = std::min(minY, m.target.y);
    EXPECT_NEAR(minY, 16, 1e-9);
    const auto sim = simulate(r.value().program, part, kSetupA, test::testProfile(), test::testTools(), simOpts());
    EXPECT_FALSE(sim.hasErrors()) << dump(sim.diagnostics);
}

TEST(CamPocket, NonCentreCuttingToolRampsOrIsRefused) {
    ToolLibrary lib;
    Tool t = test::endMill("em.6nc", 7, 6, 22, 3);
    t.centerCutting = false;
    ASSERT_TRUE(lib.add(t));
    const auto part = gripPanel();
    auto r = generateProgram(part, kSetupA, test::testProfile(), lib, opts());
    ASSERT_TRUE(r) << r.error().message;
    const auto sim = simulate(r.value().program, part, kSetupA, test::testProfile(), lib, simOpts());
    EXPECT_FALSE(sim.hasErrors()) << dump(sim.diagnostics);
    // The plunging program of a centre-cutting tool is caught when the tool is not centre-cutting.
    auto plunging = generateProgram(part, kSetupA, test::testProfile(), test::testTools(), opts());
    ASSERT_TRUE(plunging);
    ToolLibrary nc = test::testTools();
    ToolLibrary lib2;
    for (auto tl : nc.tools()) {
        if (tl.number == 4) tl.centerCutting = false;
        ASSERT_TRUE(lib2.add(tl));
    }
    const auto sim2 = simulate(plunging.value().program, part, kSetupA, test::testProfile(), lib2, simOpts());
    EXPECT_TRUE(has(sim2.diagnostics, "cam.sim.plunge_not_centre_cutting")) << dump(sim2.diagnostics);
    // A 20 x 20 pocket leaves no straight run (C is 14 x 14, a square): refused.
    auto sq = test::panel();
    domain::Pocket pk;
    pk.contour = {{100, 100}, {120, 100}, {120, 120}, {100, 120}};
    pk.depth = 5;
    sq.operations.push_back(test::op("square", pk));
    auto r3 = generateProgram(sq, kSetupA, test::testProfile(), lib, opts());
    ASSERT_FALSE(r3);
    EXPECT_NE(r3.error().message.find("ramp"), std::string::npos) << r3.error().message;
}

// Precedence: the operation's own Pocket::cornerRadius (6) wins over the option (4 here, or 0);
// no rounded-corner warning. em.6 (d 6 <= 2 x 6) mills it and the simulator expects R = 6 corners.
TEST(CamPocket, ModelCornerRadiusTakesPrecedenceOverTheOption) {
    auto part = gripPanel();
    std::get<domain::Pocket>(part.operations[0].shape).cornerRadius = 6;
    EXPECT_DOUBLE_EQ(effectivePocketCornerRadius(std::get<domain::Pocket>(part.operations[0].shape), 4), 6);
    EXPECT_DOUBLE_EQ(effectivePocketCornerRadius(domain::Pocket{}, 4), 4);
    for (double option : {0.0, 4.0}) {
        auto r = generateProgram(part, kSetupA, test::testProfile(), test::testTools(), opts(option));
        ASSERT_TRUE(r) << r.error().message;
        EXPECT_EQ(r.value().plan.machinableNow.size(), 1u);
        EXPECT_EQ(r.value().toolsUsed, std::vector<int>{4});
        EXPECT_FALSE(has(r.value().diagnostics, "cam.pocket.rounded_corner"));
        auto frame = makeSetupFrame(part, kSetupA);
        ASSERT_TRUE(frame);
        const auto t = pocketTarget(part, frame.value(), part.operations[0], option);
        EXPECT_DOUBLE_EQ(t.cornerRadius, 6);
        EXPECT_TRUE(t.radiusFromModel);
        const auto sim = simulate(r.value().program, part, kSetupA, test::testProfile(), test::testTools(),
                                  simOpts(option));
        EXPECT_FALSE(sim.hasErrors()) << dump(sim.diagnostics);
    }
    // A model radius of 25 on the 40 mm wide pocket: 2 x 25 = 50 > 40, unsupported, naming the pocket's radius.
    std::get<domain::Pocket>(part.operations[0].shape).cornerRadius = 25;
    auto plan = planSetup(part, kSetupA, 4);
    ASSERT_TRUE(plan);
    ASSERT_EQ(plan.value().unsupported.size(), 1u);
    EXPECT_NE(plan.value().unsupported[0].reason.find("pocket's corner radius 25 mm is more than half its 40 mm side"),
              std::string::npos)
        << plan.value().unsupported[0].reason;
}
