// SPDX-License-Identifier: AGPL-3.0-only
#include "test_support.hpp"

#include <chrono>
#include <cmath>

using namespace fos::nesting;
using namespace fos::nesting::test;
using fos::geometry::Vec2;

namespace {

fos::geometry::Polygon2 hexagon(double r) {
    fos::geometry::Polygon2 p;
    for (int k = 0; k < 6; ++k) {
        const double a = fos::core::degToRad(60.0 * k);
        p.vertices.push_back({r + r * std::cos(a), r + r * std::sin(a)});
    }
    return p;
}

/// 60 instances, mixed convex and concave. Outline areas (mm^2):
///   12 x rect 600x400 = 2 880 000      8 x rect 800x300 (grain, Half) = 1 920 000
///   10 x L 500x500 arm 200 (500^2 - 300^2 = 160 000) = 1 600 000
///    8 x right triangle 600x400 (120 000) = 960 000
///    8 x regular hexagon r = 200 (3*sqrt(3)/2 * 200^2 = 103 923) = 831 384
///    6 x U 400x300 with 200x200 notch (80 000) = 480 000
///    8 x trapezoid 500/300 x 300 (120 000) = 960 000
///   total = 9 631 384
Job mixedJob() {
    Job job;
    Sheet s = makeSheet("board", 2800, 2070);
    s.margins = {10, 10, 10, 10};
    job.sheets.push_back(s);
    job.partSpacing = 8; // 6 mm cutter + 2 mm clearance
    job.edgeSpacing = 4;
    job.parts.push_back(makePart("rect", rectPoly(600, 400), 12, RotationRule::Quarter));
    job.parts.push_back(makePart("rail", rectPoly(800, 300), 8, RotationRule::Half));
    job.parts.push_back(makePart("L", poly({{0, 0}, {500, 0}, {500, 200}, {200, 200}, {200, 500}, {0, 500}}), 10,
                                 RotationRule::Quarter));
    job.parts.push_back(makePart("tri", poly({{0, 0}, {600, 0}, {0, 400}}), 8, RotationRule::Quarter));
    Part hex = makePart("hex", hexagon(200), 8, RotationRule::Step);
    hex.rotationStepDeg = 30;
    job.parts.push_back(hex);
    job.parts.push_back(makePart("U", poly({{0, 0}, {400, 0}, {400, 300}, {300, 300}, {300, 100}, {100, 100}, {100, 300}, {0, 300}}),
                                 6, RotationRule::Quarter));
    job.parts.push_back(makePart("trap", poly({{0, 0}, {500, 0}, {400, 300}, {100, 300}}), 8, RotationRule::Quarter));
    return job;
}

} // namespace

TEST(NestLarge, MixedSixtyPartJob) {
    const Job job = mixedJob();
    NestOptions opt;
    opt.iterations = 6;
    const auto t0 = std::chrono::steady_clock::now();
    const NestResult r = nestValid(job, opt);
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    EXPECT_EQ(r.placedCount, 60);
    EXPECT_TRUE(r.unplaced.empty());
    EXPECT_NEAR(r.totalPartArea, 9631384.0, 1.0);
    // Placement area per sheet: (2800 - 28) x (2070 - 28) = 2772 x 2042 = 5 660 424 mm^2, so at least
    // ceil(9 631 384 / 5 660 424) = 2 sheets. Accept the lower bound + 1.
    EXPECT_LE(r.sheets.size(), 3u);
    // Threshold: the first (full) sheet must carry >= 60% of its area. Every part is surrounded by a
    // 4 mm half-spacing band (perimeter ~1600-2000 mm -> ~7000 mm^2, ~5% of a 120 000 mm^2 part), and
    // triangles/hexagons/notches leave unavoidable gaps between straight-edged neighbours; plain
    // bottom-left-fill is commonly reported at 60-80% on such mixes.
    ASSERT_FALSE(r.sheets.empty());
    EXPECT_GE(r.sheets[0].utilisation, 0.60);
    std::printf("mixed job: %zu sheets, utilisation %.3f, first sheet %.3f, last used length %.1f mm, %.2f s, %d iterations\n",
                r.sheets.size(), r.utilisation, r.sheets[0].utilisation, r.sheets.back().usedLength, secs, r.iterationsRun);
    EXPECT_LT(secs, 5.0);
}

TEST(NestLarge, DeterministicForFixedSeedAndIterations) {
    const Job job = mixedJob();
    NestOptions opt;
    opt.iterations = 3;
    opt.seed = 777;
    const NestResult a = nestValid(job, opt);
    const NestResult b = nestValid(job, opt);
    ASSERT_EQ(a.sheets.size(), b.sheets.size());
    for (std::size_t s = 0; s < a.sheets.size(); ++s) {
        ASSERT_EQ(a.sheets[s].placements.size(), b.sheets[s].placements.size());
        for (std::size_t i = 0; i < a.sheets[s].placements.size(); ++i) {
            const auto &p = a.sheets[s].placements[i], &q = b.sheets[s].placements[i];
            EXPECT_EQ(p.partId, q.partId);
            EXPECT_EQ(p.instance, q.instance);
            EXPECT_EQ(p.rotationDeg, q.rotationDeg);
            EXPECT_EQ(p.translation, q.translation); // bit-identical
        }
    }
    EXPECT_EQ(a.iterationsRun, 3);
}
