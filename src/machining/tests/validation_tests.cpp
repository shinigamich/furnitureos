// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/domain/fixtures.hpp"
#include "fos/machining/validation.hpp"

#include <gtest/gtest.h>

using namespace fos;
using core::Severity;
using domain::Face;
using domain::Hole;

namespace {
/// 600 x 400 x 18 test panel.
domain::Part panel() {
    domain::Part p;
    p.id = domain::PartId("p1");
    p.name = "Panel";
    p.length = 600;
    p.width = 400;
    p.thickness = 18;
    return p;
}
domain::Operation op(const char* id, domain::OperationShape s, const char* source = "user") {
    return {domain::OperationId(id), std::move(s), source, ""};
}
core::Diagnostics check(std::vector<domain::Operation> ops, machining::ValidationOptions o = {}) {
    auto p = panel();
    p.operations = std::move(ops);
    return machining::validateOperations(p, o);
}
int count(const core::Diagnostics& ds, const std::string& code) {
    int n = 0;
    for (const auto& d : ds) n += d.code == code;
    return n;
}
} // namespace

TEST(Validation, CleanOperationsProduceNoDiagnostics) {
    domain::Pocket pk{Face::A, {{200, 100}, {300, 100}, {300, 200}, {200, 200}}, 6, false};
    const auto ds = check({op("h1", Hole{Face::A, 100, 100, 5, 13, false}),
                           op("h2", Hole{Face::L1, 100, 9, 8, 30, false}), // centred in 18 mm
                           op("h3", Hole{Face::B, 300, 300, 7, 0, true}),
                           op("g1", domain::Groove{Face::B, 0, 50, 600, 50, 4, 8}), // runs out at both ends: fine
                           op("p1", pk)});
    EXPECT_TRUE(ds.empty()) << (ds.empty() ? "" : ds[0].message);
}

TEST(Validation, FixtureLeftSideIsClean) {
    const auto model = domain::fixtures::basicCabinet();
    for (const auto& part : model.assemblies[0].parts) EXPECT_TRUE(machining::validateOperations(part).empty());
}

TEST(Validation, HoleTooDeepHasSpecificMessage) {
    const auto ds = check({op("h1", Hole{Face::A, 100, 100, 5, 20, false})});
    ASSERT_EQ(ds.size(), 1u);
    EXPECT_EQ(ds[0].code, "machining.hole.too_deep");
    EXPECT_EQ(ds[0].severity, Severity::Error);
    EXPECT_EQ(ds[0].entityId, "p1");
    EXPECT_EQ(ds[0].message,
              "Hole 'h1' at (100, 100) on face A of 'Panel' is 20 mm deep but the panel is only 18 mm thick.");
    // Exactly the thickness is not "too deep"; an edge hole is limited by the width (400).
    EXPECT_TRUE(check({op("h1", Hole{Face::A, 100, 100, 5, 18, false})}).empty());
    EXPECT_EQ(count(check({op("h1", Hole{Face::L1, 100, 9, 5, 401, false})}), "machining.hole.too_deep"), 1);
    // Through holes ignore the stored depth.
    EXPECT_TRUE(check({op("h1", Hole{Face::A, 100, 100, 5, 99, true})}).empty());
}

TEST(Validation, HoleOutOfBounds) {
    const auto ds = check({op("h1", Hole{Face::A, 650, 100, 5, 13, false})}); // u = 650 > L = 600
    ASSERT_EQ(ds.size(), 1u);
    EXPECT_EQ(ds[0].code, "machining.hole.out_of_bounds");
    EXPECT_EQ(ds[0].severity, Severity::Error);
    EXPECT_NE(ds[0].message.find("(650, 100)"), std::string::npos);
    EXPECT_NE(ds[0].message.find("'Panel'"), std::string::npos);
    EXPECT_NE(ds[0].message.find("0..600"), std::string::npos);
    EXPECT_EQ(count(check({op("h1", Hole{Face::B, 100, -1, 5, 13, false})}), "machining.hole.out_of_bounds"), 1);
    // Edge face W1: u runs along the width (0..400).
    EXPECT_EQ(count(check({op("h1", Hole{Face::W1, 450, 9, 5, 13, false})}), "machining.hole.out_of_bounds"), 1);
    EXPECT_TRUE(check({op("h1", Hole{Face::W1, 350, 9, 5, 13, false})}).empty());
}

TEST(Validation, HoleTooCloseToEdgeIsAWarningAndConfigurable) {
    // Centre 5 mm from the edge, radius 2.5 -> 2.5 mm of material < default 3 mm.
    auto ds = check({op("h1", Hole{Face::A, 5, 100, 5, 13, false})});
    ASSERT_EQ(ds.size(), 1u);
    EXPECT_EQ(ds[0].code, "machining.hole.too_close_to_edge");
    EXPECT_EQ(ds[0].severity, Severity::Warning);
    EXPECT_NE(ds[0].message.find("only 2.5 mm"), std::string::npos) << ds[0].message;
    // 6 - 2.5 = 3.5 mm >= 3 mm: fine. Exactly 3 mm (centre 5.5): fine.
    EXPECT_TRUE(check({op("h1", Hole{Face::A, 6, 100, 5, 13, false})}).empty());
    EXPECT_TRUE(check({op("h1", Hole{Face::A, 5.5, 100, 5, 13, false})}).empty());
    // Far edge: 400 - 396 - 2.5 = 1.5 mm.
    EXPECT_EQ(count(check({op("h1", Hole{Face::A, 100, 396, 5, 13, false})}), "machining.hole.too_close_to_edge"), 1);
    // Configurable: with 2 mm required, 2.5 mm passes; with 10 mm required, 3.5 mm fails.
    machining::ValidationOptions o;
    o.minEdgeMaterial = 2;
    EXPECT_TRUE(check({op("h1", Hole{Face::A, 5, 100, 5, 13, false})}, o).empty());
    o.minEdgeMaterial = 10;
    EXPECT_EQ(count(check({op("h1", Hole{Face::A, 6, 100, 5, 13, false})}, o), "machining.hole.too_close_to_edge"), 1);
    // Wall breaks out: centre 2, radius 2.5 -> -0.5 mm.
    ds = check({op("h1", Hole{Face::A, 2, 100, 5, 13, false})});
    ASSERT_EQ(ds.size(), 1u);
    EXPECT_NE(ds[0].message.find("breaks out of the panel edge by 0.5 mm"), std::string::npos) << ds[0].message;
}

TEST(Validation, EdgeHoleCentringAndThickness) {
    // Thickness 18 -> centre v = 9. v = 7 is 2 mm off centre (hole spans 4.5..9.5, inside).
    auto ds = check({op("h1", Hole{Face::L1, 100, 7, 5, 30, false})});
    ASSERT_EQ(ds.size(), 1u);
    EXPECT_EQ(ds[0].code, "machining.hole.edge_hole_off_center");
    EXPECT_EQ(ds[0].severity, Severity::Warning);
    EXPECT_NE(ds[0].message.find("2 mm off the centre of the 18 mm"), std::string::npos) << ds[0].message;
    // v = 2, radius 2.5: spans -0.5..4.5 -> leaves the panel.
    ds = check({op("h1", Hole{Face::W2, 100, 2, 5, 30, false})});
    ASSERT_EQ(ds.size(), 1u);
    EXPECT_EQ(ds[0].code, "machining.hole.edge_hole_outside_thickness");
    EXPECT_EQ(ds[0].severity, Severity::Error);
    // A 20 mm hole can never fit an 18 mm edge, even when centred.
    EXPECT_EQ(count(check({op("h1", Hole{Face::L2, 100, 9, 20, 30, false})}),
                    "machining.hole.edge_hole_outside_thickness"), 1);
    // Along the edge the edge-distance warning still applies: u = 4, r = 2.5 -> 1.5 mm.
    EXPECT_EQ(count(check({op("h1", Hole{Face::L1, 4, 9, 5, 30, false})}), "machining.hole.too_close_to_edge"), 1);
}

TEST(Validation, InvalidSizesAndDuplicateIds) {
    auto ds = check({op("h1", Hole{Face::A, 100, 100, 0, 13, false})});
    ASSERT_EQ(ds.size(), 1u);
    EXPECT_EQ(ds[0].code, "machining.hole.invalid_size");
    EXPECT_EQ(count(check({op("h1", Hole{Face::A, 100, 100, -5, 13, false})}), "machining.hole.invalid_size"), 1);
    EXPECT_EQ(count(check({op("h1", Hole{Face::A, 100, 100, 5, 0, false})}), "machining.hole.invalid_size"), 1);
    EXPECT_EQ(count(check({op("g", domain::Groove{Face::A, 0, 50, 600, 50, 0, 8})}), "machining.groove.invalid_size"), 1);
    EXPECT_EQ(count(check({op("g", domain::Groove{Face::A, 0, 50, 600, 50, 4, -1})}), "machining.groove.invalid_size"), 1);
    EXPECT_EQ(count(check({op("g", domain::Groove{Face::A, 10, 50, 10, 50, 4, 8})}), "machining.groove.invalid_size"), 1);

    ds = check({op("dup", Hole{Face::A, 100, 100, 5, 13, false}), op("dup", Hole{Face::A, 200, 100, 5, 13, false}),
                op("ok", Hole{Face::A, 300, 100, 5, 13, false})});
    ASSERT_EQ(ds.size(), 1u);
    EXPECT_EQ(ds[0].code, "machining.operation.duplicate_id");
    EXPECT_NE(ds[0].message.find("'dup' is used by 2 operations of 'Panel'"), std::string::npos) << ds[0].message;
}

TEST(Validation, CollisionOppositeFaces) {
    // Same (x, y) from A and B: depths 10 + 10 = 20 > 18 -> the holes meet.
    auto ds = check({op("a", Hole{Face::A, 100, 100, 5, 10, false}), op("b", Hole{Face::B, 100, 100, 5, 10, false})});
    ASSERT_EQ(ds.size(), 1u);
    EXPECT_EQ(ds[0].code, "machining.hole.collision");
    EXPECT_EQ(ds[0].severity, Severity::Error);
    EXPECT_NE(ds[0].message.find("'a'"), std::string::npos);
    EXPECT_NE(ds[0].message.find("'b'"), std::string::npos);
    EXPECT_NE(ds[0].message.find("'Panel'"), std::string::npos);
    // 10 + 8 = 18: they only touch -> no collision. 10 + 7 leaves 1 mm.
    EXPECT_TRUE(check({op("a", Hole{Face::A, 100, 100, 5, 10, false}), op("b", Hole{Face::B, 100, 100, 5, 8, false})}).empty());
    // Deep enough but laterally apart: centres 5 mm apart, radii 2.5 + 2.5 -> touching only.
    EXPECT_TRUE(check({op("a", Hole{Face::A, 100, 100, 5, 10, false}), op("b", Hole{Face::B, 105, 100, 5, 10, false})}).empty());
    // 4 mm apart -> walls overlap by 1 mm.
    EXPECT_EQ(count(check({op("a", Hole{Face::A, 100, 100, 5, 10, false}), op("b", Hole{Face::B, 104, 100, 5, 10, false})}),
                    "machining.hole.collision"), 1);
}

TEST(Validation, CollisionSameFaceAndThroughHoles) {
    EXPECT_EQ(count(check({op("a", Hole{Face::A, 100, 100, 5, 10, false}), op("b", Hole{Face::A, 104, 100, 5, 3, false})}),
                    "machining.hole.collision"), 1);
    // A through hole meets a shallow hole drilled from the other face at the same place.
    EXPECT_EQ(count(check({op("a", Hole{Face::A, 100, 100, 5, 0, true}), op("b", Hole{Face::B, 100, 100, 8, 2, false})}),
                    "machining.hole.collision"), 1);
}

TEST(Validation, CollisionEdgeHoleCrossingFaceHole) {
    // Edge hole on L1 at u=100, v=9, d=8, depth 30: cylinder along +Y, y 0..30,
    // cross-section (x-100)^2 + (z-9)^2 <= 16, i.e. z from 5 to 13.
    const auto edge = op("e", Hole{Face::L1, 100, 9, 8, 30, false});
    // Face-A hole at (100, 20), depth 6: z from 12 to 18 -> overlaps z 12..13 on the axis plane.
    EXPECT_EQ(count(check({edge, op("f", Hole{Face::A, 100, 20, 5, 6, false})}), "machining.hole.collision"), 1);
    // Depth 5: z from 13 to 18 -> touches the top of the edge hole only.
    EXPECT_TRUE(check({edge, op("f", Hole{Face::A, 100, 20, 5, 5, false})}).empty());
    // Beyond the end of the edge hole: y from 37.5 to 42.5 > 30.
    EXPECT_TRUE(check({edge, op("f", Hole{Face::A, 100, 40, 5, 10, false})}).empty());
    // Straddling the flat end: face hole centre y = 32, r = 2.5 reaches y = 29.5 < 30; at y = 30
    // its chord along x is +-sqrt(2.5^2 - 2^2) = +-1.5 around x = 100, well inside the edge hole.
    EXPECT_EQ(count(check({edge, op("f", Hole{Face::A, 100, 32, 5, 10, false})}), "machining.hole.collision"), 1);
    // Centre y = 33: nearest wall at y = 30.5 > 30 -> clear.
    EXPECT_TRUE(check({edge, op("f", Hole{Face::A, 100, 33, 5, 10, false})}).empty());
    // Sideways: face hole at x = 106.4: |dx| = 6.4 < 4 + 2.5 = 6.5 -> the cylinders overlap
    // (both axes are perpendicular, a common point exists on the line x in [102.4, 103.9...]).
    EXPECT_EQ(count(check({edge, op("f", Hole{Face::A, 106.4, 20, 5, 12, false})}), "machining.hole.collision"), 1);
    // x = 106.6: |dx| = 6.6 > 6.5 -> clear.
    EXPECT_TRUE(check({edge, op("f", Hole{Face::A, 106.6, 20, 5, 12, false})}).empty());
    // Perpendicular edge holes (L1 and W1) crossing near the corner: W1 hole at u(y)=20, v=9,
    // d=8, depth 120 runs along +X through x 0..120 and passes the L1 hole (y 0..30 at x=100).
    EXPECT_EQ(count(check({edge, op("w", Hole{Face::W1, 20, 9, 8, 120, false})}), "machining.hole.collision"), 1);
    EXPECT_TRUE(check({edge, op("w", Hole{Face::W1, 20, 9, 8, 90, false})}).empty()); // ends at x = 90 < 96
}

TEST(Validation, CollisionWithinSameSourceCanBeIgnored) {
    // E.g. a connector housing crossing its own bolt hole.
    std::vector<domain::Operation> ops{op("e", Hole{Face::L1, 100, 9, 8, 34, false}, "cab1/hw.cam1"),
                                       op("f", Hole{Face::B, 100, 34, 15, 12.5, false}, "cab1/hw.cam1")};
    EXPECT_TRUE(check(ops).empty());
    machining::ValidationOptions o;
    o.ignoreCollisionsWithinSameSource = false;
    EXPECT_EQ(count(check(ops, o), "machining.hole.collision"), 1);
    // "user" operations are never exempt.
    ops[0].source = ops[1].source = "user";
    EXPECT_EQ(count(check(ops), "machining.hole.collision"), 1);
    o = {};
    o.checkCollisions = false;
    EXPECT_TRUE(check(ops, o).empty());
}

TEST(Validation, Grooves) {
    // Centre line 1 mm from the edge with 4 mm width: band from v = -1 to 3 -> outside.
    auto ds = check({op("g", domain::Groove{Face::B, 0, 1, 600, 1, 4, 8})});
    ASSERT_EQ(ds.size(), 1u);
    EXPECT_EQ(ds[0].code, "machining.groove.out_of_bounds");
    EXPECT_NE(ds[0].message.find("'Panel'"), std::string::npos);
    // v = 2: band 0..4 -> exactly inside.
    EXPECT_TRUE(check({op("g", domain::Groove{Face::B, 0, 2, 600, 2, 4, 8})}).empty());
    // End point beyond the panel.
    EXPECT_EQ(count(check({op("g", domain::Groove{Face::B, 0, 50, 610, 50, 4, 8})}), "machining.groove.out_of_bounds"), 1);
    // Diagonal groove whose corner leaves the face at the origin.
    EXPECT_EQ(count(check({op("g", domain::Groove{Face::B, 0, 0, 100, 100, 4, 8})}), "machining.groove.out_of_bounds"), 1);
    // Depth equal to the thickness would cut the panel in two.
    ds = check({op("g", domain::Groove{Face::B, 0, 50, 600, 50, 4, 18})});
    ASSERT_EQ(ds.size(), 1u);
    EXPECT_EQ(ds[0].code, "machining.groove.too_deep");
    EXPECT_NE(ds[0].message.find("18 mm deep"), std::string::npos);
    EXPECT_TRUE(check({op("g", domain::Groove{Face::B, 0, 50, 600, 50, 4, 17})}).empty());
}

TEST(Validation, PocketContours) {
    auto pocket = [](std::vector<geometry::Vec2> c, double depth = 5, bool through = false) {
        return op("pk", domain::Pocket{Face::A, std::move(c), depth, through});
    };
    // Too few points.
    auto ds = check({pocket({{10, 10}, {50, 10}})});
    ASSERT_EQ(ds.size(), 1u);
    EXPECT_EQ(ds[0].code, "machining.pocket.invalid_contour");
    EXPECT_NE(ds[0].message.find("2 point(s)"), std::string::npos);
    // Bow-tie: edge 1 (10,10)-(110,110) crosses edge 3 (110,10)-(10,110) at (60,60).
    ds = check({pocket({{10, 10}, {110, 110}, {110, 10}, {10, 110}})});
    ASSERT_EQ(ds.size(), 1u);
    EXPECT_EQ(ds[0].code, "machining.pocket.invalid_contour");
    EXPECT_NE(ds[0].message.find("edge 1"), std::string::npos) << ds[0].message;
    EXPECT_NE(ds[0].message.find("edge 3"), std::string::npos) << ds[0].message;
    // Repeated point, collinear (zero area), fold-back spike.
    EXPECT_EQ(count(check({pocket({{10, 10}, {50, 10}, {50, 10}, {50, 50}})}), "machining.pocket.invalid_contour"), 1);
    EXPECT_EQ(count(check({pocket({{10, 10}, {50, 10}, {90, 10}})}), "machining.pocket.invalid_contour"), 1);
    EXPECT_EQ(count(check({pocket({{10, 10}, {90, 10}, {50, 10}, {50, 50}})}), "machining.pocket.invalid_contour"), 1);
    // Valid shapes: triangle, L-shape (concave).
    EXPECT_TRUE(check({pocket({{10, 10}, {50, 10}, {30, 40}})}).empty());
    EXPECT_TRUE(check({pocket({{10, 10}, {110, 10}, {110, 40}, {40, 40}, {40, 110}, {10, 110}})}).empty());
    // Clockwise is a warning.
    ds = check({pocket({{10, 10}, {10, 50}, {50, 50}, {50, 10}})});
    ASSERT_EQ(ds.size(), 1u);
    EXPECT_EQ(ds[0].code, "machining.pocket.contour_clockwise");
    EXPECT_EQ(ds[0].severity, Severity::Warning);
    // Out of bounds vertex (u = 610 > 600), too deep, non-positive depth, through ignores depth.
    ds = check({pocket({{500, 10}, {610, 10}, {610, 50}, {500, 50}})});
    ASSERT_EQ(ds.size(), 1u);
    EXPECT_EQ(ds[0].code, "machining.pocket.out_of_bounds");
    EXPECT_NE(ds[0].message.find("(610, 10)"), std::string::npos);
    EXPECT_EQ(count(check({pocket({{10, 10}, {50, 10}, {30, 40}}, 19)}), "machining.pocket.too_deep"), 1);
    EXPECT_EQ(count(check({pocket({{10, 10}, {50, 10}, {30, 40}}, 0)}), "machining.pocket.invalid_size"), 1);
    EXPECT_TRUE(check({pocket({{10, 10}, {50, 10}, {30, 40}}, 0, true)}).empty());
}

TEST(Validation, ContourHelpers) {
    EXPECT_NEAR(machining::signedArea({{0, 0}, {4, 0}, {4, 3}, {0, 3}}), 12, 1e-12);
    EXPECT_NEAR(machining::signedArea({{0, 0}, {0, 3}, {4, 3}, {4, 0}}), -12, 1e-12);
    EXPECT_TRUE(machining::contourProblem({{0, 0}, {4, 0}, {4, 3}, {0, 3}}).empty());
    EXPECT_FALSE(machining::contourProblem({}).empty());
    // A contour touching itself at a vertex (two triangles sharing the point (2,2)) is rejected.
    EXPECT_FALSE(machining::contourProblem({{0, 0}, {2, 2}, {4, 0}, {4, 4}, {2, 2}, {0, 4}}).empty());
}

TEST(Validation, DegeneratePartDoesNotCrash) {
    domain::Part p; // all dimensions zero
    p.name = "Empty";
    p.operations = {op("h", Hole{Face::A, 1, 1, 5, 2, false})};
    const auto ds = machining::validateOperations(p);
    EXPECT_TRUE(core::hasErrors(ds));
}
