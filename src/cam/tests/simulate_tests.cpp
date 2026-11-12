// SPDX-License-Identifier: AGPL-3.0-only
#include "cam_test_fixtures.hpp"

#include "fos/cam/generate.hpp"
#include "fos/cam/simulate.hpp"

#include <gtest/gtest.h>

using namespace fos;
using namespace fos::cam;
using domain::Face;

namespace {
const cam::Setup kSetupB{Face::B, {10, 20, 0}};
const char* kFront = "cab1/side.left/op.pin.front";

Program leftSideProgram() {
    auto r = generateProgram(test::leftSide(), kSetupB, test::testProfile(), test::testTools());
    if (!r) throw std::logic_error(r.error().message);
    return r.value().program;
}

SimulationReport run(const Program& p, const ToolLibrary& tools = test::testTools(),
                     const MachineProfile& m = test::testProfile(), const Setup& s = kSetupB) {
    return simulate(p, test::leftSide(), s, m, tools);
}

const core::Diagnostic* find(const SimulationReport& r, const std::string& code) {
    for (const auto& d : r.diagnostics)
        if (d.code == code) return &d;
    return nullptr;
}
} // namespace

// Motions (see generate test): rapids 50->23 (27), 5->23 (18), Y 543->57 (486), 5->23 (18), 23->50 (27)
// = 576 mm; the first horizontal rapid from the unknown start is not counted.
// Cuts: each plunge runs from the clearance plane Z 23 to Z 5 = 18 mm -> 2 x 18 = 36 mm.
// Time: 576 / 20000 * 60 = 1.728 s + 36 / 1500 * 60 = 1.44 s + one tool change 10 s = 13.168 s.
TEST(CamSimulate, ValidProgramPassesWithTimeEstimate) {
    auto r = run(leftSideProgram());
    EXPECT_FALSE(r.hasErrors()) << (r.diagnostics.empty() ? "" : r.diagnostics[0].message);
    EXPECT_NEAR(r.rapidLength, 576, 1e-9);
    EXPECT_NEAR(r.cutLength, 36, 1e-9);
    EXPECT_NEAR(r.estimatedSeconds, 13.168, 1e-9);
    EXPECT_EQ(r.toolChanges, 1);
    EXPECT_GT(r.cellsRemoved, 0u);
}

TEST(CamSimulate, HoleShiftedThreeMillimetresIsCaught) {
    auto p = leftSideProgram();
    for (auto& m : p.motions)
        if (m.source.str() == kFront && (m.kind == MotionKind::Rapid || m.kind == MotionKind::Linear)) m.target.x += 3;
    auto r = run(p);
    const auto* notCut = find(r, "cam.sim.hole_not_cut");
    ASSERT_NE(notCut, nullptr);
    EXPECT_NE(notCut->message.find(kFront), std::string::npos);
    EXPECT_NE(notCut->message.find("machine X 356, Y 543"), std::string::npos);
    const auto* gouge = find(r, "cam.sim.gouge");
    ASSERT_NE(gouge, nullptr);
    EXPECT_NE(gouge->message.find(std::string("motions of operation '") + kFront + "'"), std::string::npos);
    EXPECT_NE(gouge->message.find("down to Z 5 mm"), std::string::npos);
}

TEST(CamSimulate, PlungeTwoMillimetresTooDeepIsCaught) {
    auto p = leftSideProgram();
    ASSERT_EQ(p.motions[5].kind, MotionKind::Linear);
    p.motions[5].target.z = 3; // floor is Z 5
    auto r = run(p);
    const auto* deep = find(r, "cam.sim.too_deep");
    ASSERT_NE(deep, nullptr);
    EXPECT_NE(deep->message.find(std::string("Operation '") + kFront + "' is cut down to Z 3 mm, 2 mm below its floor Z 5 mm"),
              std::string::npos)
        << deep->message;
    EXPECT_EQ(find(r, "cam.sim.hole_not_cut"), nullptr);
}

TEST(CamSimulate, EnvelopeViolation) {
    // Stock at X 2200: front pin at X 2200 + 346 = 2546 > 2500.
    const cam::Setup far{Face::B, {2200, 20, 0}};
    auto g = generateProgram(test::leftSide(), far, test::testProfile(), test::testTools());
    ASSERT_TRUE(g);
    auto r = run(g.value().program, test::testTools(), test::testProfile(), far);
    const auto* env = find(r, "cam.sim.envelope");
    ASSERT_NE(env, nullptr);
    EXPECT_NE(env->message.find("(2546, 543, 50)"), std::string::npos) << env->message;
}

TEST(CamSimulate, RapidBelowClearanceOverStock) {
    auto p = leftSideProgram();
    p.motions[4].target.z = 19; // rapid down to 19 < 23 over the stock
    auto r = run(p);
    EXPECT_NE(find(r, "cam.sim.rapid_below_clearance"), nullptr);
}

TEST(CamSimulate, CuttingWithSpindleOff) {
    auto p = leftSideProgram();
    p.motions.erase(p.motions.begin() + 2); // remove SpindleOn
    auto r = run(p);
    EXPECT_NE(find(r, "cam.sim.spindle_off_cut"), nullptr);
}

TEST(CamSimulate, FluteTooShortFeedAndRpmLimits) {
    auto p = leftSideProgram();
    ToolLibrary shortTools;
    auto t = test::drill("drill.5", 1, 5, 10); // 10 mm flute, 13 mm hole
    ASSERT_TRUE(shortTools.add(t));
    EXPECT_NE(find(run(p, shortTools), "cam.sim.flute_length"), nullptr);

    p.motions[5].feed = 20000; // > 10000
    p.motions[2].rpm = 30000;  // > 24000
    auto r = run(p);
    EXPECT_NE(find(r, "cam.sim.feed_limit"), nullptr);
    EXPECT_NE(find(r, "cam.sim.rpm_limit"), nullptr);
}

TEST(CamSimulate, SpoilboardLimitAndUnknownTool) {
    auto p = leftSideProgram();
    p.motions[8].target.z = -1.5; // 1.5 mm into the spoilboard, limit 1 mm
    p.motions[1].toolNumber = 42;
    auto r = run(p, test::testTools());
    EXPECT_NE(find(r, "cam.sim.spoilboard_limit"), nullptr);
    EXPECT_NE(find(r, "cam.sim.unknown_tool"), nullptr);
}

TEST(CamSimulate, MissingInitialRetractAndSpindleLeftOn) {
    auto p = leftSideProgram();
    p.motions.erase(p.motions.begin()); // no Z-only retract
    p.motions.pop_back();               // no spindle off
    auto r = run(p);
    EXPECT_NE(find(r, "cam.sim.position_unknown"), nullptr);
    EXPECT_NE(find(r, "cam.sim.spindle_on_at_end"), nullptr);
}

TEST(CamSimulate, MissingOperationIsReportedNotCut) {
    auto p = leftSideProgram();
    // Drop the back pin: motions 7..9 belong to it.
    p.motions.erase(p.motions.begin() + 7, p.motions.begin() + 10);
    auto r = run(p);
    const auto* d = find(r, "cam.sim.hole_not_cut");
    ASSERT_NE(d, nullptr);
    EXPECT_NE(d->message.find("op.pin.back"), std::string::npos);
    EXPECT_NE(d->message.find("untouched"), std::string::npos);
}

TEST(CamSimulate, FaceMismatch) {
    auto p = leftSideProgram();
    auto r = run(p, test::testTools(), test::testProfile(), cam::Setup{Face::A, {10, 20, 0}});
    EXPECT_NE(find(r, "cam.sim.setup_mismatch"), nullptr);
}

// Face-B groove from (0,50) to (600,50) on the 600 x 400 test panel, stock origin (100,100,0):
// machine Y = 100 + 400 - 50 = 450. Shifting every groove motion by +2 mm in Y must be caught.
TEST(CamSimulate, GrooveOnFaceBAndLateralShift) {
    auto part = test::panel();
    part.operations.push_back(test::op("g", domain::Groove{Face::B, 0, 50, 600, 50, 6, 4}));
    const cam::Setup s{Face::B, {100, 100, 0}};
    auto g = generateProgram(part, s, test::testProfile(), test::testTools());
    ASSERT_TRUE(g) << g.error().message;
    auto p = g.value().program;
    std::size_t cuts = 0;
    for (const auto& m : p.motions)
        if (m.kind == MotionKind::Linear) {
            EXPECT_NEAR(m.target.y, 450, 1e-9);
            ++cuts;
        }
    EXPECT_EQ(cuts, 4u); // one pass (6 = 6), levels Z 15 and 14
    auto ok = simulate(p, part, s, test::testProfile(), test::testTools());
    EXPECT_FALSE(ok.hasErrors()) << (ok.diagnostics.empty() ? "" : ok.diagnostics.back().message);
    for (auto& m : p.motions)
        if (m.source.str() == "g") m.target.y += 2;
    auto bad = simulate(p, part, s, test::testProfile(), test::testTools());
    EXPECT_NE(find(bad, "cam.sim.groove_not_cut"), nullptr);
    EXPECT_NE(find(bad, "cam.sim.gouge"), nullptr);
}
