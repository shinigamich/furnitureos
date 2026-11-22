// SPDX-License-Identifier: AGPL-3.0-only
// Worktop builder (worktop.hpp): run extent, overhangs, joints, cut-outs and their rules.
#include "test_support.hpp"

#include "fos/project/worktop.hpp"

namespace fos::project::test {
namespace {

/// `count` base cabinets (600 wide each) side by side starting at x = `x0`.
Project kitchen(int count, double x0 = 0) {
    Project p = newProject(meta());
    for (int i = 0; i < count; ++i) {
        Item item = baseCabinetItem("cab" + std::to_string(i + 1));
        item.cabinet()->placement = geometry::Transform::translation({x0 + 600.0 * i, 0, 0});
        p.items.push_back(item);
    }
    return p;
}

std::vector<std::string> ids(int count) {
    std::vector<std::string> v;
    for (int i = 0; i < count; ++i) v.push_back("cab" + std::to_string(i + 1));
    return v;
}

/// Bounds of the generated cabinet cab1 in its own frame (independent of the worktop code).
geometry::Aabb cabinetBounds(const Project& p) {
    auto a = generateItem(p, "cab1");
    EXPECT_TRUE(a.ok());
    return a.ok() ? a.value().bounds() : geometry::Aabb{};
}

TEST(ProjectWorktop, SinglePieceOverTwoCabinets) {
    Project p = kitchen(2, 100);
    const geometry::Aabb cab = cabinetBounds(p);
    WorktopRequest req;
    req.itemIds = {"cab2", "cab1"}; // any order
    req.leftOverhang = 10;
    req.rightOverhang = 30;
    auto plan = addWorktop(p, req);
    ASSERT_TRUE(plan.ok()) << plan.error().message;
    const WorktopPlan& w = plan.value();
    // Run: cab1 at x 100 .. 700, cab2 700 .. 1300 (cabinet bounds x = [0, 600] each, checked below).
    EXPECT_NEAR(cab.min.x, 0, 1e-9);
    EXPECT_NEAR(cab.max.x, 600, 1e-9);
    // Length = (1300 + 30) - (100 - 10) = 1240; left end at project x = 90. Plan coordinates are
    // in the frame of the FIRST LISTED cabinet (cab2 at x = 700): run start 100 - 700 = -600,
    // left end 90 - 700 = -610.
    EXPECT_NEAR(w.length, 1240, 1e-9);
    EXPECT_NEAR(w.left, -610, 1e-9);
    EXPECT_NEAR(w.runStart, -600, 1e-9);
    // Depth = back (cab.max.y - 0) - front (cab.min.y - 20).
    EXPECT_NEAR(w.depth, cab.max.y - cab.min.y + 20, 1e-9);
    EXPECT_NEAR(w.top, cab.max.z, 1e-9);
    EXPECT_EQ(w.runOrder, (std::vector<std::string>{"cab1", "cab2"}));
    EXPECT_TRUE(w.joints.empty());
    ASSERT_TRUE(w.addedMaterial.has_value());
    EXPECT_EQ(w.thickness, 38.0);
    // Default libraries: ABS 22 mm wide bands do not cover 38 mm -> generic 45 mm band added.
    ASSERT_TRUE(w.addedEdgeBand.has_value());
    ASSERT_EQ(p.items.size(), 3u);

    const Resolved r = resolved(p);
    EXPECT_FALSE(core::hasErrors(r.diagnostics));
    const domain::Part* part = findPart(r, "worktop/panel");
    ASSERT_NE(part, nullptr);
    EXPECT_EQ(part->role, domain::PartRole::Worktop);
    EXPECT_NEAR(part->length, 1240, 1e-9);
    EXPECT_NEAR(part->thickness, 38, 1e-9);
    EXPECT_EQ(part->edge(domain::EdgeSide::L1), domain::EdgeBandId(kGenericWorktopEdgeBand));
    EXPECT_FALSE(part->edge(domain::EdgeSide::L2).has_value());
    EXPECT_FALSE(part->edge(domain::EdgeSide::W1).has_value());
    EXPECT_FALSE(part->edge(domain::EdgeSide::W2).has_value());
    const geometry::Aabb b = part->bounds().transformed(p.findItem("worktop")->placement()); // project frame
    EXPECT_NEAR(b.min.x, 90, 1e-9);
    EXPECT_NEAR(b.max.x, 1330, 1e-9);
    EXPECT_NEAR(b.min.y, cab.min.y - 20, 1e-9);
    EXPECT_NEAR(b.min.z, cab.max.z, 1e-9);
    EXPECT_NEAR(b.max.z, cab.max.z + 38, 1e-9);
}

TEST(ProjectWorktop, LongRunIsSplitWithButtJoint) {
    Project p = kitchen(8); // 4800 mm
    WorktopRequest req;
    req.itemIds = ids(8);
    auto plan = planWorktop(p, req);
    ASSERT_TRUE(plan.ok()) << plan.error().message;
    // Usable stock 4100 - 10 - 10 (default trims) = 4080 < 4800 -> 2 equal pieces, joint at 2400.
    EXPECT_NEAR(plan.value().usableStockLength, 4080, 1e-9);
    ASSERT_EQ(plan.value().joints.size(), 1u);
    EXPECT_NEAR(plan.value().joints[0], 2400, 1e-9);
    ASSERT_EQ(plan.value().items.size(), 2u);
    EXPECT_EQ(plan.value().items[0].id(), "worktop.1");
    EXPECT_EQ(plan.value().items[1].name(), "Worktop 2/2");
    const auto& notes = plan.value().notes;
    EXPECT_TRUE(std::any_of(notes.begin(), notes.end(), [](const core::Diagnostic& d) { return d.code == "project.worktop.butt_joint"; }));
}

TEST(ProjectWorktop, JointMovesAwayFromSinkAndCutoutIsInSecondPiece) {
    Project p = kitchen(8);
    WorktopRequest req;
    req.itemIds = ids(8);
    req.cutouts.push_back({WorktopCutoutKind::Sink, 2400, 800, 480, 10, std::nullopt});
    auto plan = addWorktop(p, req);
    ASSERT_TRUE(plan.ok()) << plan.error().message;
    // Sink x 2000 .. 2800; joint keep-out 1850 .. 2950; ideal 2400 is inside; both zone ends are
    // 550 away, the left one (1850) is taken. Pieces 1850 and 2950 (both <= 4080).
    ASSERT_EQ(plan.value().joints.size(), 1u);
    EXPECT_NEAR(plan.value().joints[0], 1850, 1e-9);
    const Resolved r = resolved(p);
    EXPECT_FALSE(core::hasErrors(r.diagnostics));
    const domain::Part* first = findPart(r, "worktop.1/panel");
    const domain::Part* second = findPart(r, "worktop.2/panel");
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    EXPECT_NEAR(first->length, 1850, 1e-9);
    EXPECT_NEAR(second->length, 2950, 1e-9);
    EXPECT_FALSE(first->shape.has_value()); // no cut-out -> plain rectangle
    ASSERT_TRUE(second->shape.has_value());
    ASSERT_EQ(second->shape->cutouts.size(), 1u);
    const geometry::Rect2 c = geometry::bounds(second->shape->cutouts[0]);
    // In piece 2: x = 2000 - 1850 = 150 .. 950; centred in depth: y = (depth - 480) / 2.
    const double depth = plan.value().depth;
    EXPECT_NEAR(c.min.x, 150, 1e-6);
    EXPECT_NEAR(c.max.x, 950, 1e-6);
    EXPECT_NEAR(c.min.y, (depth - 480) / 2, 1e-6);
    EXPECT_NEAR(c.max.y, (depth + 480) / 2, 1e-6);
    // The front band stays on the front segment of the shaped part.
    EXPECT_FALSE(second->shape->outerEdges.empty());
}

TEST(ProjectWorktop, RuleOfThumbViolationsAreRefused) {
    Project p = kitchen(3);
    WorktopRequest req;
    req.itemIds = ids(3);
    // Sink centre 300, 600 long -> starts at x = 0 < 100 mm from the left end.
    req.cutouts = {{WorktopCutoutKind::Sink, 300, 600, 480, 0, std::nullopt}};
    auto r1 = planWorktop(p, req);
    ASSERT_FALSE(r1.ok());
    EXPECT_NE(r1.error().message.find("left end"), std::string::npos) << r1.error().message;
    EXPECT_NE(r1.error().message.find("rule of thumb"), std::string::npos);
    // Sink 600 .. 1100 and hob 1150 .. 1650: 50 mm apart < 100.
    req.cutouts = {{WorktopCutoutKind::Sink, 850, 500, 450, 0, std::nullopt},
                   {WorktopCutoutKind::Hob, 1400, 500, 450, 0, std::nullopt}};
    auto r2 = planWorktop(p, req);
    ASSERT_FALSE(r2.ok());
    EXPECT_NE(r2.error().message.find("50 mm from the sink"), std::string::npos) << r2.error().message;
    // Front setback 20 < 50.
    req.cutouts = {{WorktopCutoutKind::Hob, 900, 560, 490, 0, 20.0}};
    auto r3 = planWorktop(p, req);
    ASSERT_FALSE(r3.ok());
    EXPECT_NE(r3.error().message.find("front edge"), std::string::npos) << r3.error().message;
    EXPECT_TRUE(p.items.size() == 3u);
}

TEST(ProjectWorktop, RunErrors) {
    Project p = kitchen(2);
    WorktopRequest req;
    req.itemIds = {"cab1", "nope"};
    EXPECT_EQ(planWorktop(p, req).error().code, core::ErrorCode::NotFound);
    req.itemIds = {"cab1", "cab1"};
    EXPECT_FALSE(planWorktop(p, req).ok());
    req.itemIds = ids(2);
    p.items[1].cabinet()->placement = geometry::Transform::translation({600, 0, 0}) * geometry::Transform::rotationZ(90);
    auto rot = planWorktop(p, req);
    ASSERT_FALSE(rot.ok());
    EXPECT_NE(rot.error().message.find("rotated differently"), std::string::npos);
    p.items[1].cabinet()->placement = geometry::Transform::translation({500, 0, 0}); // 100 mm overlap
    auto overlap = planWorktop(p, req);
    ASSERT_FALSE(overlap.ok());
    EXPECT_NE(overlap.error().message.find("overlap by 100 mm"), std::string::npos) << overlap.error().message;
    p.items[1].cabinet()->placement = geometry::Transform::translation({600, 0, 50});
    auto height = planWorktop(p, req);
    ASSERT_FALSE(height.ok());
    EXPECT_NE(height.error().message.find("different heights"), std::string::npos);
    p.items[1].cabinet()->placement = geometry::Transform::translation({650, 0, 0}); // 50 mm gap: warning only
    auto gap = planWorktop(p, req);
    ASSERT_TRUE(gap.ok()) << gap.error().message;
    EXPECT_EQ(gap.value().notes.front().code, "project.worktop.gap");
    req.frontOverhang = -5;
    EXPECT_FALSE(planWorktop(p, req).ok());
}

TEST(ProjectWorktop, CommandIsUndoable) {
    Document doc(kitchen(2));
    WorktopRequest req;
    req.itemIds = ids(2);
    req.materialId = defaults::kParticleboard18; // 18 mm: the 22 mm ABS bands cover it
    ASSERT_TRUE(doc.execute(commands::addWorktop(req)).ok());
    EXPECT_EQ(doc.project().items.size(), 3u);
    // Band chosen: first library band by id with width >= 18: "eb.generic.abs04" < "eb.generic.abs2".
    const Resolved r = resolved(doc.project());
    const domain::Part* part = findPart(r, "worktop/panel");
    ASSERT_NE(part, nullptr);
    EXPECT_EQ(part->edge(domain::EdgeSide::L1), domain::EdgeBandId(defaults::kAbs04));
    EXPECT_EQ(doc.project().libraries.materials.size(), defaultMaterials().size()); // nothing added
    EXPECT_FALSE(doc.execute(commands::addWorktop(req)).ok()); // id "worktop" now taken
    ASSERT_TRUE(doc.undo().ok());
    EXPECT_EQ(doc.project().items.size(), 2u);
}

} // namespace
} // namespace fos::project::test
