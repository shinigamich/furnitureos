// SPDX-License-Identifier: AGPL-3.0-only
/// Checks 3 and 4: part interference, groove engagement, interference between assemblies.
#include "test_support.hpp"

#include <algorithm>
#include <cmath>

namespace fos::validation::test {
namespace {
using domain::Face;
using domain::Groove;
using domain::Operation;
using domain::OperationId;
using geometry::Transform;
using geometry::Vec3;

TEST(PartInterference, ShelfThreeMillimetresTooLong) {
    domain::Model m = domain::fixtures::basicCabinet();
    // Shelf x 18..785 (767 long) vs right side x 782..800: overlap 3 along X; along Y 540,
    // along Z 18 -> penetration 3 along X. The shelf is the longer part along X (767 vs 18).
    part(m, "cab1/shelf.1").length = 767;
    const ValidationReport r = validateModel(m);
    const Finding f = only(r, "validation.interference.parts");
    EXPECT_EQ(f.message, "'Shelf' penetrates 'Right side' by 3 mm along X.");
    EXPECT_EQ(f.severity, core::Severity::Error);
    EXPECT_EQ(f.assemblyId, "cab1");
    EXPECT_EQ(f.partId, "cab1/shelf.1");
    EXPECT_EQ(f.relatedIds, std::vector<std::string>{"cab1/side.right"});
    EXPECT_EQ(r.counts.errors, 1u) << toText(r);
    EXPECT_EQ(r.stats.partPairsTested, 1u); // only shelf/right side has overlapping boxes
    EXPECT_GE(r.stats.broadPhaseChecks, 1u);
    EXPECT_LE(r.stats.broadPhaseChecks, 15u);

    ValidationOptions off;
    off.checkPartInterference = false;
    EXPECT_EQ(count(validateModel(m, off), "validation.interference.parts"), 0u);
}

TEST(PartInterference, TouchingAndWithinToleranceIsNotInterference) {
    domain::Model m = domain::fixtures::basicCabinet();
    part(m, "cab1/shelf.1").length = 764.005; // 0.005 overlap <= 0.01 tolerance
    EXPECT_EQ(count(validateModel(m), "validation.interference.parts"), 0u);
    part(m, "cab1/shelf.1").length = 764.02; // 0.02 > 0.01
    EXPECT_EQ(only(validateModel(m), "validation.interference.parts").message,
              "'Shelf' penetrates 'Right side' by 0.02 mm along X."); // below 1 mm: 3 decimals
}

TEST(PartInterference, RotatedPartHandDerived) {
    // Same geometry as Obb.RotatedBoxHandDerived: penetration 25 sqrt(3) - 40 = 3.30127 mm
    // along v = (-sin30, cos30, 0). The plate extends 2 (50 s + 50 c) = 136.6 mm along v,
    // the bar 10 mm -> the plate is named as the penetrating part.
    const double c = std::cos(core::degToRad(30)), s = 0.5;
    const Vec3 u{c, s, 0}, v{-s, c, 0}, z{0, 0, 1};
    const Vec3 centre = Vec3{50, 50, 9} + v * 70;
    domain::Model m = libraryModel({board("plate", 100, 100), board("bar", 100, 10,
                                                                     Transform{u, v, z, centre - u * 50 - v * 5 - z * 9})});
    const ValidationReport r = validateModel(m);
    EXPECT_EQ(only(r, "validation.interference.parts").message,
              "'Board plate' penetrates 'Board bar' by 3.3 mm along (-0.500, 0.866, 0.000).");
    EXPECT_EQ(r.counts.errors, 1u) << toText(r);
}

/// basicCabinet with the back (3 mm, y 550..553) running in 3 x 8 mm grooves of both sides.
/// Left side: groove on face B (inner face x = 18), depth 8 -> x 10..18, y 550..553, z 0..720.
/// Right side: groove on face A (inner face x = 782) -> x 782..790. The shelf is narrowed to
/// 530 (y 20..550) so that it stops at the back.
domain::Model groovedBack(double backX, double backLength, double grooveWidth = 3) {
    domain::Model m = domain::fixtures::basicCabinet();
    part(m, "cab1/shelf.1").width = 530;
    domain::Part& back = part(m, "cab1/back");
    back.length = backLength;
    back.width = 684; // z 18..702, between bottom and top
    back.placement = Transform::fromAxes({1, 0, 0}, {0, 0, 1}, {backX, 553, 18});
    for (const auto& [id, face] : {std::pair{"cab1/side.left", Face::B}, std::pair{"cab1/side.right", Face::A}}) {
        Operation g;
        g.id = OperationId(std::string(id) + "/op.groove");
        g.source = "cab1/rule.back_groove";
        g.shape = Groove{face, 0, 551.5, 720, 551.5, grooveWidth, 8};
        part(m, id).operations.push_back(g);
    }
    return m;
}

TEST(PartInterference, BackInGroovesIsAccepted) {
    // Back x 11..789: inside the grooves 10..18 and 782..790 (7 mm engagement each side).
    const ValidationReport r = validateModel(groovedBack(11, 778));
    EXPECT_EQ(r.counts.errors, 0u) << toText(r);
    EXPECT_EQ(r.counts.warnings, 0u) << toText(r);
    EXPECT_EQ(r.stats.grooveEngagementsAllowed, 2u);
    EXPECT_EQ(r.stats.partPairsTested, 2u);
    // 0.05 mm beyond the groove bottom on each side is within grooveFitTolerance 0.1.
    EXPECT_EQ(validateModel(groovedBack(9.95, 780.1)).stats.grooveEngagementsAllowed, 2u);
}

TEST(PartInterference, BackStickingOutOfTheGroove) {
    // Back x 9..791: 1 mm beyond the groove bottoms at x = 10 and x = 790.
    const ValidationReport r = validateModel(groovedBack(9, 782));
    EXPECT_EQ(count(r, "validation.interference.outside_groove"), 2u) << toText(r);
    EXPECT_EQ(r.stats.grooveEngagementsAllowed, 0u);
    const auto left = findingsForEntity(r, "cab1/side.left");
    ASSERT_EQ(left.size(), 1u) << toText(r);
    EXPECT_EQ(left[0].message, "'Back panel' engages groove 'cab1/side.left/op.groove' of 'Left side' but sticks out "
                               "of the groove by 1 mm along X (groove fit tolerance 0.1 mm).");
    EXPECT_EQ(left[0].partId, "cab1/back");
    // Groove 2.6 wide (y 550.2..552.8) for a 3 mm back: 0.2 mm out on each side along Y.
    const ValidationReport narrow = validateModel(groovedBack(11, 778, 2.6));
    EXPECT_EQ(count(narrow, "validation.interference.outside_groove"), 2u);
    EXPECT_NE(narrow.diagnostics[0].message.find("by 0.2 mm along Y"), std::string::npos) << toText(narrow);
}

TEST(PartInterference, NoGrooveMeansPlainInterference) {
    domain::Model m = groovedBack(11, 778);
    part(m, "cab1/side.left").operations.resize(2); // keep the two shelf-pin holes, drop the groove
    part(m, "cab1/side.right").operations.clear();
    const ValidationReport r = validateModel(m);
    // Back x 11..789 vs left side x 0..18: the shortest separating translations are
    // X: 18 - 11 = 7;  Y: 560 - 550 = 10 (back y 550..553 inside side y 0..560);  Z: 702 - 0 = 702
    // -> 7 mm along X; the back is the longer part along X (778 vs 18).
    const auto left = findingsForEntity(r, "cab1/side.left");
    ASSERT_EQ(left.size(), 1u) << toText(r);
    EXPECT_EQ(left[0].message, "'Back panel' penetrates 'Left side' by 7 mm along X.");
    EXPECT_EQ(count(r, "validation.interference.parts"), 2u) << toText(r);
}

TEST(AssemblyInterference, ProjectFrameAndSeverity) {
    domain::Model m = domain::fixtures::basicCabinet();
    domain::Assembly second = m.assemblies[0];
    second.id = domain::AssemblyId("cab2");
    second.name = "Second cabinet";
    second.placement = Transform::translation({790, 0, 0});
    m.assemblies.push_back(second);
    // cab1 right side x 782..800 vs cab2 left side x 790..808: 10 along X (equal 18 mm extents
    // along X -> the first part of the pair, cab1's, is named).
    // cab1 back x 2..798 vs cab2 back x 792..1588, both y 560..563: overlaps X 6, Y 3, Z 716 -> 3 along Y.
    ValidationReport r = validateModel(m);
    EXPECT_EQ(r.counts.warnings, 2u) << toText(r);
    EXPECT_EQ(r.counts.errors, 0u) << toText(r);
    EXPECT_EQ(r.stats.possibleCrossAssemblyPartPairs, 36u);
    EXPECT_EQ(r.stats.crossAssemblyPartPairsTested, 2u);
    const auto list = findingsWithCode(r, "validation.interference.assemblies");
    ASSERT_EQ(list.size(), 2u);
    std::vector<std::string> msgs{list[0].message, list[1].message};
    std::sort(msgs.begin(), msgs.end());
    EXPECT_EQ(msgs[0], "'Back panel' of assembly 'Base cabinet 800' penetrates 'Back panel' of assembly 'Second "
                       "cabinet' by 3 mm along Y (project frame).");
    EXPECT_EQ(msgs[1], "'Right side' of assembly 'Base cabinet 800' penetrates 'Left side' of assembly 'Second "
                       "cabinet' by 10 mm along X (project frame).");
    ValidationOptions o;
    o.assemblyInterferenceSeverity = core::Severity::Error;
    r = validateModel(m, o);
    EXPECT_EQ(r.counts.errors, 2u);
    o.checkAssemblyInterference = false;
    EXPECT_EQ(validateModel(m, o).counts.errors, 0u);
}

TEST(Report, DeterministicOrder) {
    domain::Model m = domain::fixtures::basicCabinet();
    part(m, "cab1/shelf.1").length = 767;
    part(m, "cab1/top").thickness = 16;
    part(m, "cab1/back").name.clear();
    m.edgeBands[0].width = 16;
    const ValidationReport r1 = validateModel(m);
    EXPECT_EQ(toText(r1), toText(validateModel(m)));
    std::reverse(m.assemblies[0].parts.begin(), m.assemblies[0].parts.end());
    const ValidationReport r2 = validateModel(m);
    EXPECT_EQ(toText(r1), toText(r2));
    // Severity first, then assembly, part, code, message.
    for (std::size_t i = 1; i < r1.diagnostics.size(); ++i) {
        const auto& a = r1.diagnostics[i - 1];
        const auto& b = r1.diagnostics[i];
        EXPECT_GE(static_cast<int>(a.severity), static_cast<int>(b.severity)); // Error > Warning > Info
        if (a.severity == b.severity) {
            EXPECT_LE(a.assemblyId + "|" + a.partId, b.assemblyId + "|" + b.partId);
        }
    }
}

} // namespace
} // namespace fos::validation::test
