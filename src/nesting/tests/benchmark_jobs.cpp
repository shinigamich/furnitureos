// SPDX-License-Identifier: AGPL-3.0-only
#include "benchmark_jobs.hpp"

#include "test_support.hpp"

#include <cmath>

namespace fos::nesting::test {

namespace {

using geometry::Polygon2;

Polygon2 regular(int n, double r, double phaseDeg = 0) {
    Polygon2 p;
    for (int k = 0; k < n; ++k) {
        const double a = core::degToRad(phaseDeg + 360.0 * k / n);
        p.vertices.push_back({r + r * std::cos(a), r + r * std::sin(a)});
    }
    return p;
}

Polygon2 lShape(double s, double arm) { return poly({{0, 0}, {s, 0}, {s, arm}, {arm, arm}, {arm, s}, {0, s}}); }

Polygon2 cross(double s, double arm) {
    const double a = (s - arm) / 2, b = a + arm;
    return poly({{a, 0}, {b, 0}, {b, a}, {s, a}, {s, b}, {b, b}, {b, s}, {a, s}, {a, b}, {0, b}, {0, a}, {a, a}});
}

Sheet sheet(double l, double w) {
    Sheet s = makeSheet("board", l, w);
    s.margins = {10, 10, 10, 10};
    return s;
}

} // namespace

std::vector<BenchmarkJob> benchmarkJobs() {
    std::vector<BenchmarkJob> out;
    {   // 60 mixed convex/concave instances on 2800 x 2070 (the large-job test set).
        Job job;
        job.sheets.push_back(sheet(2800, 2070));
        job.partSpacing = 8;
        job.edgeSpacing = 4;
        job.parts.push_back(makePart("rect", rectPoly(600, 400), 12, RotationRule::Quarter));
        job.parts.push_back(makePart("rail", rectPoly(800, 300), 8, RotationRule::Half));
        job.parts.push_back(makePart("L", lShape(500, 200), 10, RotationRule::Quarter));
        job.parts.push_back(makePart("tri", poly({{0, 0}, {600, 0}, {0, 400}}), 8, RotationRule::Quarter));
        Part hex = makePart("hex", regular(6, 200), 8, RotationRule::Step);
        hex.rotationStepDeg = 30;
        job.parts.push_back(hex);
        job.parts.push_back(makePart(
            "U", poly({{0, 0}, {400, 0}, {400, 300}, {300, 300}, {300, 100}, {100, 100}, {100, 300}, {0, 300}}), 6,
            RotationRule::Quarter));
        job.parts.push_back(makePart("trap", poly({{0, 0}, {500, 0}, {400, 300}, {100, 300}}), 8, RotationRule::Quarter));
        out.push_back({"mixed60", std::move(job)});
    }
    {   // Frames with large cut-outs plus small parts that fit into those cut-outs.
        Job job;
        job.sheets.push_back(sheet(1500, 1000));
        job.partSpacing = 8;
        job.edgeSpacing = 4;
        Part frame = makePart("frame", rectPoly(700, 450), 6, RotationRule::Quarter);
        frame.holes.push_back(poly({{80, 80}, {620, 80}, {620, 370}, {80, 370}})); // 540 x 290 cut-out
        job.parts.push_back(frame);
        job.parts.push_back(makePart("block", rectPoly(240, 120), 18, RotationRule::Quarter));
        job.parts.push_back(makePart("disc", regular(8, 60, 22.5), 12, RotationRule::Quarter));
        out.push_back({"frames", std::move(job)});
    }
    {   // Grain-locked rails (0/180 only) mixed with free L-shapes and triangles on 2440 x 1220.
        Job job;
        job.sheets.push_back(sheet(2440, 1220));
        job.partSpacing = 10;
        job.edgeSpacing = 5;
        job.parts.push_back(makePart("rail", rectPoly(1200, 100), 10, RotationRule::Half));
        job.parts.push_back(makePart("L", lShape(400, 150), 14, RotationRule::Quarter));
        job.parts.push_back(makePart("tri", poly({{0, 0}, {500, 0}, {0, 300}}), 10, RotationRule::Quarter));
        out.push_back({"grain", std::move(job)});
    }
    {   // Triangles and trapezoids on a square sheet.
        Job job;
        job.sheets.push_back(sheet(1220, 1220));
        job.partSpacing = 6;
        job.edgeSpacing = 3;
        job.parts.push_back(makePart("tri", poly({{0, 0}, {400, 0}, {0, 300}}), 30, RotationRule::Quarter));
        job.parts.push_back(makePart("trap", poly({{0, 0}, {400, 0}, {300, 200}, {100, 200}}), 20, RotationRule::Quarter));
        out.push_back({"tri_trap", std::move(job)});
    }
    {   // Hexagons (30 degree steps) and plus-shaped crosses on 1000 x 1000.
        Job job;
        job.sheets.push_back(sheet(1000, 1000));
        job.partSpacing = 8;
        job.edgeSpacing = 4;
        Part hex = makePart("hex", regular(6, 110), 16, RotationRule::Step);
        hex.rotationStepDeg = 30;
        job.parts.push_back(hex);
        job.parts.push_back(makePart("cross", cross(300, 100), 12, RotationRule::Quarter));
        out.push_back({"hex_cross", std::move(job)});
    }
    return out;
}

} // namespace fos::nesting::test
