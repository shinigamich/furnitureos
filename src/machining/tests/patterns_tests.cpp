// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/machining/patterns.hpp"

#include <gtest/gtest.h>

using namespace fos;
using domain::Face;
using domain::Hole;

namespace {
const Hole& hole(const domain::Operation& op) { return std::get<Hole>(op.shape); }
} // namespace

TEST(Patterns, LineOfHoles) {
    const machining::HoleSpec spec{Face::B, 5, 13, false};
    const auto ops = machining::lineOfHoles(spec, {37, 100}, {0, 32}, 3, "side/op.line", "rule.x");
    ASSERT_EQ(ops.size(), 3u);
    EXPECT_EQ(ops[0].id.str(), "side/op.line.1");
    EXPECT_EQ(ops[1].id.str(), "side/op.line.2");
    EXPECT_EQ(ops[2].id.str(), "side/op.line.3");
    EXPECT_EQ(hole(ops[0]), (Hole{Face::B, 37, 100, 5, 13, false}));
    EXPECT_EQ(hole(ops[1]), (Hole{Face::B, 37, 132, 5, 13, false}));
    EXPECT_EQ(hole(ops[2]), (Hole{Face::B, 37, 164, 5, 13, false}));
    for (const auto& o : ops) EXPECT_EQ(o.source, "rule.x");
    EXPECT_TRUE(machining::lineOfHoles(spec, {0, 0}, {32, 0}, 0, "p").empty());
    EXPECT_TRUE(machining::lineOfHoles(spec, {0, 0}, {32, 0}, -2, "p").empty());
    // Diagonal pitch and through holes (stored depth is normalised to 0).
    const auto d = machining::lineOfHoles({Face::A, 7, 99, true}, {10, 10}, {3, 4}, 2, "p");
    EXPECT_EQ(hole(d[1]), (Hole{Face::A, 13, 14, 7, 0, true}));
    EXPECT_EQ(d[0].source, "user");
}

TEST(Patterns, System32Row) {
    machining::System32Row row;
    row.hole = {Face::B, 5, 13, false};
    row.rowOffset = 37;
    row.firstHoleOffset = 48;
    row.rangeMin = 100;
    row.rangeMax = 300;
    // Grid: 48, 80, 112, 144, 176, 208, 240, 272, 304, ... -> inside [100, 300]: 112 .. 272 (6 holes).
    auto ops = machining::system32Row(row, "s");
    ASSERT_EQ(ops.size(), 6u);
    const double expected[] = {112, 144, 176, 208, 240, 272};
    for (std::size_t i = 0; i < 6; ++i) {
        EXPECT_EQ(hole(ops[i]), (Hole{Face::B, expected[i], 37, 5, 13, false}));
        EXPECT_EQ(ops[i].id.str(), "s." + std::to_string(i + 1));
    }
    // Range limits are inclusive.
    row.rangeMin = 112;
    row.rangeMax = 272;
    EXPECT_EQ(machining::system32Row(row, "s").size(), 6u);
    // No grid point inside (113..143).
    row.rangeMin = 113;
    row.rangeMax = 143;
    EXPECT_TRUE(machining::system32Row(row, "s").empty());
    // The grid never extends below firstHoleOffset: range 0..100 -> 48, 80.
    row.rangeMin = 0;
    row.rangeMax = 100;
    ops = machining::system32Row(row, "s");
    ASSERT_EQ(ops.size(), 2u);
    EXPECT_DOUBLE_EQ(hole(ops[0]).u, 48);
    EXPECT_DOUBLE_EQ(hole(ops[1]).u, 80);
    // Row along v: (rowOffset, along).
    row.direction = machining::RowDirection::AlongV;
    ops = machining::system32Row(row, "s");
    EXPECT_EQ(hole(ops[1]), (Hole{Face::B, 37, 80, 5, 13, false}));
    // Invalid input.
    row.pitch = 0;
    EXPECT_TRUE(machining::system32Row(row, "s").empty());
    row.pitch = 32;
    row.rangeMin = 200;
    row.rangeMax = 100;
    EXPECT_TRUE(machining::system32Row(row, "s").empty());
}

TEST(Patterns, RectangularGrid) {
    const auto ops = machining::rectangularGrid({Face::A, 5, 10, false}, {10, 20}, 32, 64, 3, 2, "g");
    ASSERT_EQ(ops.size(), 6u);
    // Row by row: (10,20) (42,20) (74,20) (10,84) (42,84) (74,84)
    EXPECT_EQ(hole(ops[0]), (Hole{Face::A, 10, 20, 5, 10, false}));
    EXPECT_EQ(hole(ops[2]), (Hole{Face::A, 74, 20, 5, 10, false}));
    EXPECT_EQ(hole(ops[3]), (Hole{Face::A, 10, 84, 5, 10, false}));
    EXPECT_EQ(hole(ops[5]), (Hole{Face::A, 74, 84, 5, 10, false}));
    EXPECT_EQ(ops[5].id.str(), "g.6");
    EXPECT_TRUE(machining::rectangularGrid({}, {0, 0}, 32, 32, 0, 2, "g").empty());
    EXPECT_TRUE(machining::rectangularGrid({}, {0, 0}, 0, 32, 2, 2, "g").empty());
    // A single column does not need a u pitch.
    EXPECT_EQ(machining::rectangularGrid({}, {0, 0}, 0, 32, 1, 2, "g").size(), 2u);
}

TEST(Patterns, MirroredPairs) {
    // Mirror across u = 300 (centre line of a 600 mm long panel).
    const auto ops = machining::mirroredPairs({Face::A, 8, 12, false}, {{50, 30}, {300, 60}},
                                              machining::MirrorLine::ConstantU, 300, "m", "hw1");
    // (50,30) -> mirror (550,30); (300,60) lies on the line -> single hole.
    ASSERT_EQ(ops.size(), 3u);
    EXPECT_EQ(hole(ops[0]), (Hole{Face::A, 50, 30, 8, 12, false}));
    EXPECT_EQ(hole(ops[1]), (Hole{Face::A, 550, 30, 8, 12, false}));
    EXPECT_EQ(hole(ops[2]), (Hole{Face::A, 300, 60, 8, 12, false}));
    EXPECT_EQ(ops[2].id.str(), "m.3");
    EXPECT_EQ(ops[1].source, "hw1");
    const auto v = machining::mirroredPairs({Face::A, 8, 12, false}, {{50, 30}}, machining::MirrorLine::ConstantV, 200, "m");
    ASSERT_EQ(v.size(), 2u);
    EXPECT_EQ(hole(v[1]), (Hole{Face::A, 50, 370, 8, 12, false})); // 2*200 - 30
}

TEST(Patterns, Deterministic) {
    const auto a = machining::rectangularGrid({}, {1, 2}, 32, 32, 4, 4, "x");
    const auto b = machining::rectangularGrid({}, {1, 2}, 32, 32, 4, 4, "x");
    ASSERT_EQ(a.size(), b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        EXPECT_EQ(a[i].id, b[i].id);
        EXPECT_EQ(a[i].shape, b[i].shape);
    }
}
