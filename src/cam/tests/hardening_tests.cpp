// SPDX-License-Identifier: AGPL-3.0-only
// Regression tests for defects found by an independent review (probe programs reproduced exactly).
#include "cam_test_fixtures.hpp"

#include "fos/cam/generate.hpp"
#include "fos/cam/generic_iso.hpp"
#include "fos/cam/simulate.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <numbers>
#include <random>

using namespace fos;
using namespace fos::cam;
using domain::Face;

namespace {

const core::Diagnostic* find(const SimulationReport& r, const std::string& code) {
    for (const auto& d : r.diagnostics)
        if (d.code == code) return &d;
    return nullptr;
}

std::size_t count(const SimulationReport& r, const std::string& code) {
    std::size_t n = 0;
    for (const auto& d : r.diagnostics) n += d.code == code ? 1u : 0u;
    return n;
}

bool has(const std::string& s, const std::string& needle) { return s.find(needle) != std::string::npos; }

} // namespace

// ---- 1. safe Z below the stock top ------------------------------------------------------------
// Probe: profile safe Z 10, panel 600 x 400 x 18 at (20, 20, 0) face A up. Stock top = 0 + 18 = 18,
// clearance plane = 18 + 5 = 23 > 10. Program: G0 Z10, T2 M6, G0 X300 Y200 Z10. (300, 200) lies over
// the stock [20, 620] x [20, 420], so the rapid ends 18 - 10 = 8 mm inside the board.
TEST(CamHardening, SafeZBelowStockTopIsAnErrorAndThePostprocessorRefuses) {
    const auto part = test::panel();
    const cam::Setup setup{Face::A, {20, 20, 0}};
    const auto tools = test::testTools();
    auto m = test::testProfile();
    m.safeZ = 10;
    EXPECT_FALSE(core::hasErrors(validateProfile(m))); // 10 is inside the Z envelope [-5, 150]
    Program p;
    p.partId = part.id.str();
    p.faceUp = Face::A;
    p.safeZ = 10;
    p.clearanceZ = 23;
    p.motions = {Motion::rapidZ(10), Motion::toolChange(2), Motion::rapid({300, 200, 10})};
    const auto rep = simulate(p, part, setup, m, tools);
    const auto* sz = find(rep, "cam.sim.safe_z_below_clearance");
    ASSERT_NE(sz, nullptr);
    EXPECT_TRUE(has(sz->message, "Safe Z 10 mm")) << sz->message;
    EXPECT_TRUE(has(sz->message, "clearance plane Z 23 mm")) << sz->message;
    EXPECT_TRUE(has(sz->message, "stock top 18 mm + clearance 5 mm")) << sz->message;
    // Motion #0 (Z-only to 10 from an unknown XY) and motion #2 (first XY travel from an unknown
    // start: the whole table counts as swept) are both below the clearance plane.
    EXPECT_EQ(count(rep, "cam.sim.rapid_below_clearance"), 2u);
    bool firstXy = false;
    for (const auto& d : rep.diagnostics)
        firstXy = firstXy || (d.code == "cam.sim.rapid_below_clearance" && has(d.message, "Motion #2") &&
                              has(d.message, "unknown X/Y"));
    EXPECT_TRUE(firstXy);

    const GenericIsoPostprocessor post;
    const auto out = post.generate(PostInput{p, part, setup, m, tools});
    ASSERT_FALSE(out.ok());
    EXPECT_TRUE(has(out.error().message, "cam.sim.safe_z_below_clearance")) << out.error().message;
}

TEST(CamHardening, SafeZMustClearARaisedStockAndTheFirstTravelHeight) {
    const auto part = test::panel();
    const auto tools = test::testTools();
    auto m = test::testProfile();
    // Stock on a 10 mm sub-plate: top = 10 + 18 = 28, clearance plane 33 > safe Z 30.
    m.safeZ = 30;
    const cam::Setup raised{Face::A, {20, 20, 10}};
    Program p;
    p.faceUp = Face::A;
    p.motions = {Motion::rapidZ(30), Motion::rapid({300, 200, 30})};
    auto rep = simulate(p, part, raised, m, tools);
    EXPECT_NE(find(rep, "cam.sim.safe_z_below_clearance"), nullptr);
    EXPECT_EQ(count(rep, "cam.sim.rapid_below_clearance"), 2u); // Z 30 < 33 for both moves
    // The same program on stock at Z 0 (top 18, plane 23 <= 30) is clean.
    rep = simulate(p, part, cam::Setup{Face::A, {20, 20, 0}}, m, tools);
    EXPECT_FALSE(rep.hasErrors()) << rep.diagnostics[0].message;
    // Generator refuses the raised setup itself (safe Z below the clearance plane).
    domain::Part withHole = part;
    domain::Hole h;
    h.face = Face::A;
    h.u = 100;
    h.v = 100;
    h.diameter = 8;
    h.depth = 10;
    withHole.operations.push_back(test::op("h1", h));
    auto g = generateProgram(withHole, raised, m, tools);
    ASSERT_FALSE(g);
    EXPECT_TRUE(has(g.error().message, "below the clearance plane Z 33 mm")) << g.error().message;
}

// ---- 2a. tools smaller than twice the cell size -----------------------------------------------
// Probe: 0.4 mm end mill cuts a 400 mm slot 5 mm deep (Z 13) at Y 120.5 where no operation exists.
// Cell centres lie at Y 20 + (j + 0.5) * 0.5, i.e. at ...120.25 and 120.75: both 0.25 mm from the
// slot centre, more than its radius 0.2 -> no centre is ever touched and the gouge was invisible.
TEST(CamHardening, ToolBelowCellResolutionIsRefusedAndFinerCellsSeeTheGouge) {
    const auto part = test::panel();
    const cam::Setup setup{Face::A, {20, 20, 0}};
    auto lib = test::testTools();
    ASSERT_TRUE(lib.add(test::endMill("em.04", 9, 0.4, 22, 6)));
    const auto m = test::testProfile();
    Program q;
    q.faceUp = Face::A;
    q.motions = {Motion::rapidZ(50),
                 Motion::toolChange(9),
                 Motion::spindleOn(18000),
                 Motion::rapid({100, 120.5, 50}),
                 Motion::rapid({100, 120.5, 23}),
                 Motion::linear({100, 120.5, 13}, 1000),
                 Motion::linear({500, 120.5, 13}, 3000),
                 Motion::rapid({500, 120.5, 50}),
                 Motion::spindleOff()};
    const auto rep = simulate(q, part, setup, m, lib);
    const auto* res = find(rep, "cam.sim.tool_below_resolution");
    ASSERT_NE(res, nullptr);
    EXPECT_TRUE(has(res->message, "diameter 0.4 mm")) << res->message;
    EXPECT_TRUE(has(res->message, "cell size 0.5 mm")) << res->message;
    const GenericIsoPostprocessor post;
    EXPECT_FALSE(post.generate(PostInput{q, part, setup, m, lib}).ok());

    // With 0.2 mm cells (0.4 >= 2 x 0.2) centres at Y 120.5 +- 0.1 are inside the 0.2 mm radius:
    // the gouge is found, from X 100 - 0.2 to 500 + 0.2 (cell centres 99.9 .. 500.1), down to Z 13.
    SimulationOptions fine;
    fine.cellSize = 0.2;
    const auto rf = simulate(q, part, setup, m, lib, fine);
    EXPECT_EQ(find(rf, "cam.sim.tool_below_resolution"), nullptr);
    const auto* g = find(rf, "cam.sim.gouge");
    ASSERT_NE(g, nullptr);
    EXPECT_TRUE(has(g->message, "machine X 99.9..500.1")) << g->message;
    EXPECT_TRUE(has(g->message, "down to Z 13 mm")) << g->message;
}

// ---- 2b. the last partial strip of the stock ---------------------------------------------------
// Panel 600.4 mm long at X 20: stock X [20, 620.4]. With 0.5 mm cells the old floor(600.4 / 0.5) =
// 1200 columns ended at X 620 (last centre 619.75), dropping the strip [620, 620.4]. Now
// ceil(1200.8) = 1201 columns, the last clipped to [620, 620.4] with centre 620.2.
// A 1.5 mm end mill (r 0.75) runs along Y at X 620.9 (half off the panel) 8 mm deep (Z 10):
// |620.9 - 620.2| = 0.7 < 0.75 -> the clipped cell is cut; |620.9 - 619.75| = 1.15 > 0.75 -> the
// old last cell was not. Rows: centres at Y 20 + (j + 0.5) 0.5; at dx = 0.7 the tool disc reaches
// |dy| <= sqrt(0.75^2 - 0.7^2) = 0.269 around the ends, so the cut rows run from Y 29.75 to 400.25.
TEST(CamHardening, PartialLastStripIsSimulated) {
    auto part = test::panel();
    part.length = 600.4;
    const cam::Setup setup{Face::A, {20, 20, 0}};
    auto lib = test::testTools();
    ASSERT_TRUE(lib.add(test::endMill("em.15", 10, 1.5, 22, 10)));
    const auto m = test::testProfile();
    const double x = 620.9;
    Program s;
    s.faceUp = Face::A;
    s.motions = {Motion::rapidZ(50),           Motion::toolChange(10),
                 Motion::spindleOn(18000),     Motion::rapid({x, 30, 50}),
                 Motion::rapid({x, 30, 23}),   Motion::linear({x, 30, 10}, 1000),
                 Motion::linear({x, 400, 10}, 3000), Motion::rapid({x, 400, 50}),
                 Motion::spindleOff()};
    const auto rep = simulate(s, part, setup, m, lib);
    const auto* g = find(rep, "cam.sim.gouge");
    ASSERT_NE(g, nullptr);
    EXPECT_TRUE(has(g->message, "machine X 620.2..620.2")) << g->message;
    EXPECT_TRUE(has(g->message, "Y 29.75..400.25")) << g->message;
    EXPECT_TRUE(has(g->message, "down to Z 10 mm")) << g->message;
    // (400.25 - 29.75) / 0.5 + 1 = 742 cells, all in the clipped column.
    EXPECT_TRUE(has(g->message, ": 742 cells")) << g->message;
}

// A through hole whose footprint reaches into the clipped strip is verified there too: hole at
// u = 596.2 (d 8, part x 592.2 .. 600.2, i.e. into [600, 600.4]) on the 600.4 panel: clean.
TEST(CamHardening, HoleAtThePartialStripVerifiesClean) {
    auto part = test::panel();
    part.length = 600.4;
    domain::Hole h;
    h.face = Face::A;
    h.u = 596.2;
    h.v = 200;
    h.diameter = 8;
    h.through = true;
    part.operations.push_back(test::op("h.edge", h));
    const cam::Setup setup{Face::A, {20, 20, 0}};
    auto g = generateProgram(part, setup, test::testProfile(), test::testTools());
    ASSERT_TRUE(g) << g.error().message;
    const auto rep = simulate(g.value().program, part, setup, test::testProfile(), test::testTools());
    EXPECT_FALSE(rep.hasErrors()) << rep.diagnostics[0].message;
}

// ---- 2c. stock below Z = 0 -------------------------------------------------------------------
TEST(CamHardening, NegativeStockOriginZIsRefused) {
    auto part = test::panel();
    domain::Hole h;
    h.face = Face::A;
    h.u = 100;
    h.v = 100;
    h.diameter = 8;
    h.through = true;
    part.operations.push_back(test::op("h1", h));
    const cam::Setup s4{Face::A, {20, 20, -4}};
    auto frame = makeSetupFrame(part, s4);
    ASSERT_FALSE(frame);
    EXPECT_TRUE(has(frame.error().message, "machine Z -4 mm, below Z = 0")) << frame.error().message;
    EXPECT_FALSE(planSetup(part, s4));
    EXPECT_FALSE(generateProgram(part, s4, test::testProfile(), test::testTools()));
    // A program made for Z 0 and simulated against the Z -4 setup is rejected as a setup mismatch.
    auto g = generateProgram(part, cam::Setup{Face::A, {20, 20, 0}}, test::testProfile(), test::testTools());
    ASSERT_TRUE(g);
    const auto rep = simulate(g.value().program, part, s4, test::testProfile(), test::testTools());
    EXPECT_NE(find(rep, "cam.sim.setup_mismatch"), nullptr);
    // Z 0 itself is allowed.
    EXPECT_TRUE(makeSetupFrame(part, cam::Setup{Face::A, {20, 20, 0}}));
}

// ---- 2d. generated programs never leave the envelope -------------------------------------------
// Open groove on face A from u 0 to u 300 at v 100, 6 mm wide -> em.6 (r 3), lead-out 2 mm: the
// pass starts at u = 0 - (3 + 2) = -5. With the stock at machine X 0 that is X -5 < envelope 0.
// First motion of the groove: XY travel at safe Z 50 to (-5, 20 + 100 = 120).
TEST(CamHardening, GeneratorRefusesAGrooveLeadOutOutsideTheEnvelope) {
    auto part = test::panel();
    domain::Groove g;
    g.face = Face::A;
    g.u1 = 0;
    g.v1 = 100;
    g.u2 = 300;
    g.v2 = 100;
    g.width = 6;
    g.depth = 5;
    part.operations.push_back(test::op("g.open", g));
    auto r = generateProgram(part, cam::Setup{Face::A, {0, 20, 0}}, test::testProfile(), test::testTools());
    ASSERT_FALSE(r);
    EXPECT_EQ(r.error().code, core::ErrorCode::ConstraintViolation);
    const std::string& msg = r.error().message;
    EXPECT_TRUE(has(msg, "Operation 'g.open' of 'Test panel' needs the tool at machine X -5, Y 120, Z 50 mm")) << msg;
    EXPECT_TRUE(has(msg, "X [0, 2500]")) << msg;
    EXPECT_TRUE(has(msg, "groove lead-out (2 mm plus the tool radius")) << msg;
    // 5 mm further in, the same groove is generated and simulates clean.
    const cam::Setup ok{Face::A, {5, 20, 0}};
    auto r2 = generateProgram(part, ok, test::testProfile(), test::testTools());
    ASSERT_TRUE(r2) << r2.error().message;
    EXPECT_FALSE(simulate(r2.value().program, part, ok, test::testProfile(), test::testTools()).hasErrors());
}

// Deterministic fuzz: random holes and grooves (both faces, open and closed grooves) on random
// panels clamped near the envelope origin. Every case must either be refused by generateProgram
// with a reason, or produce a program the independent simulator finds no error in.
TEST(CamHardening, FuzzEveryGeneratedProgramSimulatesCleanOrIsRefused) {
    std::mt19937 rng(20261113u); // mt19937 output is fully specified by the standard
    auto U = [&](double a, double b) { return a + (b - a) * (static_cast<double>(rng()) / 4294967296.0); };
    const auto tools = test::testTools();
    const auto m = test::testProfile();
    int generated = 0, refused = 0, envelopeRefusals = 0;
    for (int it = 0; it < 300; ++it) {
        auto part = test::panel();
        part.length = std::round(U(100, 800));
        part.width = std::round(U(60, 500));
        part.thickness = (it % 2) ? 18 : 16;
        const Face face = (it % 3 == 0) ? Face::B : Face::A;
        const int n = 1 + static_cast<int>(rng() % 4);
        for (int k = 0; k < n; ++k) {
            const Face f = (rng() % 2) ? Face::A : Face::B;
            if (rng() % 2) {
                domain::Hole h;
                h.face = f;
                const double ds[] = {5, 8, 35};
                h.diameter = ds[rng() % 3];
                h.u = U(h.diameter / 2 + 1, part.length - h.diameter / 2 - 1);
                h.v = U(h.diameter / 2 + 1, part.width - h.diameter / 2 - 1);
                h.through = rng() % 3 == 0;
                h.depth = std::min(U(2, part.thickness), h.diameter == 35 ? 13.0 : 30.0);
                part.operations.push_back(test::op("h" + std::to_string(k), h));
            } else {
                domain::Groove g;
                g.face = f;
                g.width = U(6, 20);
                g.depth = U(2, 10);
                const bool alongU = rng() % 2, open = rng() % 2;
                if (alongU) {
                    g.v1 = g.v2 = U(g.width / 2 + 1, part.width - g.width / 2 - 1);
                    g.u1 = open ? 0 : U(10, part.length / 2 - 10);
                    g.u2 = open && rng() % 2 ? part.length : U(part.length / 2 + 10, part.length - 10);
                } else {
                    g.u1 = g.u2 = U(g.width / 2 + 1, part.length - g.width / 2 - 1);
                    g.v1 = open ? 0 : U(10, part.width / 2 - 10);
                    g.v2 = open && rng() % 2 ? part.width : U(part.width / 2 + 10, part.width - 10);
                }
                part.operations.push_back(test::op("g" + std::to_string(k), g));
            }
        }
        CamOptions o;
        if (rng() % 2) o.peckDepth = U(1, 6);
        const cam::Setup s{face, {U(0, 30), U(0, 30), 0}};
        auto r = generateProgram(part, s, m, tools, o);
        if (!r) {
            ++refused;
            EXPECT_FALSE(r.error().message.empty()) << "case " << it;
            if (has(r.error().message, "outside the envelope")) ++envelopeRefusals;
            continue;
        }
        ++generated;
        const auto rep = simulate(r.value().program, part, s, m, tools);
        for (const auto& d : rep.diagnostics)
            EXPECT_NE(d.severity, core::Severity::Error) << "case " << it << ": [" << d.code << "] " << d.message;
    }
    EXPECT_GT(generated, 150);
    EXPECT_GT(envelopeRefusals, 0); // the near-origin lead-outs are refused by the generator
    EXPECT_EQ(generated + refused, 300);
}

// ---- face-B mapping, independent of setup.cpp ----------------------------------------------------
// The simulator shares holeTarget / faceToMachineXY with the generator, so it cannot catch a wrong
// face mapping. Here the machine XY of every face-B hole of the left side is recomputed from first
// principles: domain face B is the z = 0 plane with u = local x, v = local y (operation.hpp); the
// panel is turned over about its LENGTH axis = rotation of pi about local X, then translated so the
// rotated box again has its minimum corner at the stock origin o. Expected plunge XY = o + R p + t.
// Hand check (front pin, u 346, v 37, W 560, o = (10, 20)): (10 + 346, 20 + 560 - 37) = (356, 543).
TEST(CamHardening, FaceBHoleMachineXYFromFirstPrinciples) {
    const auto part = test::leftSide();
    const cam::Setup setup{Face::B, {10, 20, 0}};
    auto g = generateProgram(part, setup, test::testProfile(), test::testTools());
    ASSERT_TRUE(g) << g.error().message;
    const double c = std::cos(std::numbers::pi), s = std::sin(std::numbers::pi);
    auto rot = [&](double x, double y, double z) {
        return std::array<double, 3>{x, c * y - s * z, s * y + c * z};
    };
    // Translation t = -min over the rotated box corners.
    double minY = 1e300, minZ = 1e300;
    for (double x : {0.0, part.length})
        for (double y : {0.0, part.width})
            for (double z : {0.0, part.thickness}) {
                const auto q = rot(x, y, z);
                minY = std::min(minY, q[1]);
                minZ = std::min(minZ, q[2]);
            }
    EXPECT_NEAR(minY, -part.width, 1e-9);
    EXPECT_NEAR(minZ, -part.thickness, 1e-9);
    int checked = 0;
    bool sawFrontPin = false;
    for (const auto& op : part.operations) {
        const auto* h = std::get_if<domain::Hole>(&op.shape);
        if (!h || h->face != Face::B) continue;
        const auto q = rot(h->u, h->v, 0.0); // face B point in the part frame: (u, v, 0)
        const double ex = setup.stockOrigin.x + q[0];
        const double ey = setup.stockOrigin.y + q[1] - minY;
        const double ez = setup.stockOrigin.z + q[2] - minZ; // face B now on top: Z = T
        EXPECT_NEAR(ez, part.thickness, 1e-9);
        bool found = false;
        for (const auto& mo : g.value().program.motions)
            if (mo.kind == MotionKind::Linear && mo.source == op.id) {
                EXPECT_NEAR(mo.target.x, ex, 1e-9) << op.id.str();
                EXPECT_NEAR(mo.target.y, ey, 1e-9) << op.id.str();
                found = true;
            }
        EXPECT_TRUE(found) << op.id.str();
        if (op.id.str() == "cab1/side.left/op.pin.front") {
            sawFrontPin = true;
            EXPECT_NEAR(ex, 356, 1e-9);
            EXPECT_NEAR(ey, 543, 1e-9);
        }
        ++checked;
    }
    EXPECT_GE(checked, 2);
    EXPECT_TRUE(sawFrontPin);
}
