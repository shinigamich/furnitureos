// SPDX-License-Identifier: AGPL-3.0-only
#include "cam_test_fixtures.hpp"

#include "fos/cam/generate.hpp"
#include "fos/cam/simulate.hpp"

#include <gtest/gtest.h>

using namespace fos;
using namespace fos::cam;
using domain::Face;

namespace {
void expectMotion(const Motion& m, MotionKind k, double x, double y, double z, const char* src = nullptr) {
    EXPECT_EQ(m.kind, k) << describe(m);
    EXPECT_NEAR(m.target.x, x, 1e-9) << describe(m);
    EXPECT_NEAR(m.target.y, y, 1e-9) << describe(m);
    EXPECT_NEAR(m.target.z, z, 1e-9) << describe(m);
    if (src) {
        EXPECT_EQ(m.source.str(), src);
    }
}
std::vector<const Motion*> linears(const Program& p) {
    std::vector<const Motion*> out;
    for (const auto& m : p.motions)
        if (m.kind == MotionKind::Linear) out.push_back(&m);
    return out;
}
} // namespace

// Left side, face B up, stock origin (10, 20, 0). Pins (346,37) and (346,523), 5 x 13 mm.
// Machine XY: (356, 543) and (356, 57). Top Z = 18, clearance Z = 18 + 5 = 23, floor Z = 18 - 13 = 5.
// Order (nearest neighbour from face origin): front pin first.
TEST(CamGenerate, LeftSideFaceBExactMotions) {
    const auto part = test::leftSide();
    auto r = generateProgram(part, {Face::B, {10, 20, 0}}, test::testProfile(), test::testTools());
    ASSERT_TRUE(r) << r.error().message;
    const auto& m = r.value().program.motions;
    ASSERT_EQ(m.size(), 12u);
    EXPECT_TRUE(m[0].zOnly);
    EXPECT_DOUBLE_EQ(m[0].target.z, 50);
    EXPECT_EQ(m[1].kind, MotionKind::ToolChange);
    EXPECT_EQ(m[1].toolNumber, 1);
    EXPECT_EQ(m[2].kind, MotionKind::SpindleOn);
    EXPECT_DOUBLE_EQ(m[2].rpm, 6000);
    const char* front = "cab1/side.left/op.pin.front";
    const char* back = "cab1/side.left/op.pin.back";
    expectMotion(m[3], MotionKind::Rapid, 356, 543, 50, front);
    expectMotion(m[4], MotionKind::Rapid, 356, 543, 23, front);
    expectMotion(m[5], MotionKind::Linear, 356, 543, 5, front);
    EXPECT_DOUBLE_EQ(m[5].feed, 1500);
    expectMotion(m[6], MotionKind::Rapid, 356, 543, 23, front);
    expectMotion(m[7], MotionKind::Rapid, 356, 57, 23, back);
    expectMotion(m[8], MotionKind::Linear, 356, 57, 5, back);
    expectMotion(m[9], MotionKind::Rapid, 356, 57, 23, back);
    expectMotion(m[10], MotionKind::Rapid, 356, 57, 50);
    EXPECT_EQ(m[11].kind, MotionKind::SpindleOff);
    EXPECT_EQ(r.value().toolsUsed, std::vector<int>{1});
    EXPECT_EQ(r.value().program.clearanceZ, 23);
}

TEST(CamGenerate, LeftSideFaceAUpHasNoCuttingAndReportsOtherSetup) {
    const auto part = test::leftSide();
    auto r = generateProgram(part, {Face::A, {10, 20, 0}}, test::testProfile(), test::testTools());
    ASSERT_TRUE(r);
    EXPECT_TRUE(linears(r.value().program).empty());
    int other = 0;
    for (const auto& d : r.value().diagnostics) other += d.code == "cam.setup.needs_other_setup";
    EXPECT_EQ(other, 2);
}

// 8 mm through hole in an 18 mm panel: floor = 0 - 0.5 (breakthrough) = -0.5.
TEST(CamGenerate, ThroughHoleBreakthroughAndSpoilboardLimit) {
    auto part = test::panel();
    part.operations.push_back(test::op("thru", domain::Hole{Face::A, 100, 100, 8, 0, true}));
    auto r = generateProgram(part, {Face::A, {}}, test::testProfile(), test::testTools());
    ASSERT_TRUE(r) << r.error().message;
    auto l = linears(r.value().program);
    ASSERT_EQ(l.size(), 1u);
    expectMotion(*l[0], MotionKind::Linear, 100, 100, -0.5);
    EXPECT_EQ(r.value().toolsUsed, std::vector<int>{2});
    auto sim = simulate(r.value().program, part, {Face::A, {}}, test::testProfile(), test::testTools());
    EXPECT_FALSE(sim.hasErrors()) << (sim.diagnostics.empty() ? "" : sim.diagnostics[0].message);

    CamOptions o;
    o.breakthroughAllowance = 2; // > 1 mm spoilboard limit
    auto bad = generateProgram(part, {Face::A, {}}, test::testProfile(), test::testTools(), o);
    ASSERT_FALSE(bad);
    EXPECT_NE(bad.error().message.find("exceeds the spoilboard limit 1 mm"), std::string::npos);
}

// 5 x 13 hole, peck 5: pecks to depth 5, 10, 13 -> Z 13, 8, 5; retract to Z 23 after each.
TEST(CamGenerate, PeckDrilling) {
    auto part = test::panel();
    part.operations.push_back(test::op("h", domain::Hole{Face::A, 50, 60, 5, 13, false}));
    CamOptions o;
    o.peckDepth = 5;
    auto r = generateProgram(part, {Face::A, {}}, test::testProfile(), test::testTools(), o);
    ASSERT_TRUE(r);
    const auto& m = r.value().program.motions;
    // 0 rapidZ, 1 T, 2 S, 3 rapid XY at 50, 4 rapid to 23, then peck sequence.
    ASSERT_GE(m.size(), 11u);
    expectMotion(m[5], MotionKind::Linear, 50, 60, 13);
    expectMotion(m[6], MotionKind::Rapid, 50, 60, 23);
    expectMotion(m[7], MotionKind::Linear, 50, 60, 8);
    expectMotion(m[8], MotionKind::Rapid, 50, 60, 23);
    expectMotion(m[9], MotionKind::Linear, 50, 60, 5);
    expectMotion(m[10], MotionKind::Rapid, 50, 60, 23);
    auto sim = simulate(r.value().program, part, {Face::A, {}}, test::testProfile(), test::testTools());
    EXPECT_FALSE(sim.hasErrors());
}

// Groove on face A from (0,50) to (600,50), 8 wide, 8 deep; both ends open. Stock origin (100,100,0).
// Tool em.6: d = 6, r = 3, DOC 3. Levels: depth 3, 6, 8 -> Z 15, 12, 10.
// span = 8 - 6 = 2; stepover <= 0.5 * 6 = 3 -> passes = ceil(2/3) + 1 = 2 at offsets -1, +1 -> Y 149, 151.
// Centre line extended by r + lead-out = 5: X from 100 - 5 = 95 to 700 + 5 = 705.
TEST(CamGenerate, GroovePassesAndStepDown) {
    auto part = test::panel();
    part.operations.push_back(test::op("g", domain::Groove{Face::A, 0, 50, 600, 50, 8, 8}));
    const cam::Setup setup{Face::A, {100, 100, 0}};
    auto r = generateProgram(part, setup, test::testProfile(), test::testTools());
    ASSERT_TRUE(r) << r.error().message;
    auto l = linears(r.value().program);
    ASSERT_EQ(l.size(), 12u);
    const double ys[] = {149, 151}, zs[] = {15, 12, 10};
    for (int lv = 0; lv < 3; ++lv)
        for (int p = 0; p < 2; ++p) {
            const auto idx = static_cast<std::size_t>((lv * 2 + p) * 2);
            expectMotion(*l[idx], MotionKind::Linear, 95, ys[p], zs[lv]);
            EXPECT_DOUBLE_EQ(l[idx]->feed, 1000);
            expectMotion(*l[idx + 1], MotionKind::Linear, 705, ys[p], zs[lv]);
            EXPECT_DOUBLE_EQ(l[idx + 1]->feed, 3000);
        }
    auto sim = simulate(r.value().program, part, setup, test::testProfile(), test::testTools());
    EXPECT_FALSE(sim.hasErrors()) << (sim.diagnostics.empty() ? "" : sim.diagnostics.back().message);
}

// Closed groove (100,200)-(300,200), 6 wide, 5 deep: tool centre from 103 to 297, levels Z 15, 13.
TEST(CamGenerate, ClosedGrooveWarnsAboutRoundedEnds) {
    auto part = test::panel();
    part.operations.push_back(test::op("g", domain::Groove{Face::A, 100, 200, 300, 200, 6, 5}));
    auto r = generateProgram(part, {Face::A, {}}, test::testProfile(), test::testTools());
    ASSERT_TRUE(r);
    int rounded = 0;
    for (const auto& d : r.value().diagnostics) rounded += d.code == "cam.groove.rounded_end";
    EXPECT_EQ(rounded, 2);
    auto l = linears(r.value().program);
    ASSERT_EQ(l.size(), 4u);
    expectMotion(*l[0], MotionKind::Linear, 103, 200, 15);
    expectMotion(*l[1], MotionKind::Linear, 297, 200, 15);
    expectMotion(*l[3], MotionKind::Linear, 297, 200, 13);
    auto sim = simulate(r.value().program, part, {Face::A, {}}, test::testProfile(), test::testTools());
    EXPECT_FALSE(sim.hasErrors()) << (sim.diagnostics.empty() ? "" : sim.diagnostics.back().message);
}

TEST(CamGenerate, MissingToolIsASpecificError) {
    auto part = test::panel();
    part.operations.push_back(test::op("h7", domain::Hole{Face::A, 50, 50, 7, 10, false}));
    auto r = generateProgram(part, {Face::A, {}}, test::testProfile(), test::testTools());
    ASSERT_FALSE(r);
    EXPECT_EQ(r.error().message,
              "Hole 'h7' of 'Test panel': No drill of diameter 7 mm in the tool library (available: 5, 8, 35).");
}

TEST(CamGenerate, EdgeHoleIsReportedUnsupported) {
    auto part = test::panel();
    part.operations.push_back(test::op("edge", domain::Hole{Face::W1, 200, 9, 8, 30, false}));
    auto r = generateProgram(part, {Face::A, {}}, test::testProfile(), test::testTools());
    ASSERT_TRUE(r);
    ASSERT_EQ(r.value().plan.unsupported.size(), 1u);
    bool warned = false;
    for (const auto& d : r.value().diagnostics) warned = warned || d.code == "cam.setup.unsupported";
    EXPECT_TRUE(warned);
    EXPECT_TRUE(linears(r.value().program).empty());
}

TEST(CamGenerate, ToolAboveMachineLimitsIsRefused) {
    auto profile = test::testProfile();
    profile.maxRpm = 5000; // drill.5 runs at 6000
    auto r = generateProgram(test::leftSide(), {Face::B, {10, 20, 0}}, profile, test::testTools());
    ASSERT_FALSE(r);
    EXPECT_NE(r.error().message.find("6000 rpm"), std::string::npos);
}

TEST(CamGenerate, InvalidPartIsRefused) {
    auto part = test::panel();
    part.operations.push_back(test::op("deep", domain::Hole{Face::A, 50, 50, 5, 20, false})); // 20 > 18
    auto r = generateProgram(part, {Face::A, {}}, test::testProfile(), test::testTools());
    ASSERT_FALSE(r);
    EXPECT_NE(r.error().message.find("machining validation error"), std::string::npos);
}

TEST(CamGenerate, Deterministic) {
    const auto part = test::leftSide();
    auto a = generateProgram(part, {Face::B, {10, 20, 0}}, test::testProfile(), test::testTools());
    auto b = generateProgram(part, {Face::B, {10, 20, 0}}, test::testProfile(), test::testTools());
    ASSERT_TRUE(a && b);
    ASSERT_EQ(a.value().program.motions.size(), b.value().program.motions.size());
    for (std::size_t i = 0; i < a.value().program.motions.size(); ++i)
        EXPECT_EQ(describe(a.value().program.motions[i]), describe(b.value().program.motions[i]));
}
