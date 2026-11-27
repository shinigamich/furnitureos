// SPDX-License-Identifier: AGPL-3.0-only
// Inner-drawer editor view-model: editor state <-> InnerDrawerSpec, the one undoable section
// command, covering-front check, tree text and the generator findings shown by the editor.
//
// Base cabinet template: root = one leaf with 1 adjustable shelf behind a door pair (hinges on
// both sides). Inner drawers need a leaf without shelves, so the shelf is removed first.
#include "fos/app/inner_drawer_rows.hpp"
#include "fos/app/interior_rows.hpp"
#include "test_support.hpp"

namespace fos::app::test {
namespace {

const furniture::CabinetDefinition& cabinet(const Session& s) { return *s.project().items.at(0).cabinet(); }

int innerDrawerParts(const Session& s, const char* slotPart) {
    int n = 0;
    for (const auto& p : s.resolved()->model.assemblies.at(0).parts)
        n += p.id.str().find("inner_drawer") != std::string::npos && p.id.str().find(slotPart) != std::string::npos;
    return n;
}

TEST(InnerDrawerInput, RoundTripAndParseErrors) {
    InnerDrawerInput off;
    auto none = innerDrawerSpec(off);
    ASSERT_TRUE(none.ok());
    EXPECT_FALSE(none.value().has_value());

    InnerDrawerInput in;
    in.enabled = true;
    in.heights = QStringLiteral("200; 1*; 2*");
    in.front = furniture::InnerDrawerFront::Facia;
    in.handle = furniture::InnerFaciaHandle::None;
    in.bottom = furniture::DrawerBottomMount::NailedUnder;
    in.joint = furniture::JointFastener::Confirmat;
    auto spec = innerDrawerSpec(in);
    ASSERT_TRUE(spec.ok());
    ASSERT_TRUE(spec.value().has_value());
    const auto& s = *spec.value();
    ASSERT_EQ(s.heights.size(), 3u);
    EXPECT_EQ(s.heights[0], furniture::SectionSize::fixed(200));
    EXPECT_EQ(s.heights[2], furniture::SectionSize::weight(2));
    EXPECT_EQ(s.slide.str(), hardware::ids::kSlideBallBearing);
    const InnerDrawerInput back = innerDrawerInput(s);
    EXPECT_TRUE(back.enabled);
    EXPECT_EQ(back.heights, QStringLiteral("200; 1*; 2*"));
    EXPECT_EQ(back.front, furniture::InnerDrawerFront::Facia);
    EXPECT_EQ(back.handle, furniture::InnerFaciaHandle::None);
    EXPECT_EQ(back.bottom, furniture::DrawerBottomMount::NailedUnder);
    EXPECT_EQ(back.joint, furniture::JointFastener::Confirmat);
    EXPECT_EQ(innerDrawersText(s), QStringLiteral("inner drawers: 3 with facias"));

    InnerDrawerInput bad = in;
    bad.heights = QStringLiteral("200;;1*");
    EXPECT_FALSE(innerDrawerSpec(bad).ok());
}

TEST(InnerDrawerCover, CoveringFrontOfNestedSections) {
    furniture::Section root;
    root.front = furniture::FrontSpec{}; // Door
    root.split = furniture::SplitDirection::Horizontal;
    root.children.resize(2);
    root.children[1].front = furniture::FrontSpec{};
    root.children[1].front->kind = furniture::FrontKind::Flap;
    EXPECT_EQ(coveringFront(root, "1"), furniture::FrontKind::Door);
    EXPECT_EQ(coveringFront(root, "2"), furniture::FrontKind::Flap); // nearest front wins
    EXPECT_FALSE(coveringFront(root, "3").has_value());
    EXPECT_TRUE(innerDrawersEditable(root, "1"));
    EXPECT_FALSE(innerDrawersEditable(root, "2")); // flap
    EXPECT_FALSE(innerDrawersEditable(root, ""));  // split
    root.front.reset();
    EXPECT_FALSE(innerDrawersEditable(root, "1")); // open section
    root.children[0].innerDrawers = furniture::InnerDrawerSpec{};
    EXPECT_TRUE(innerDrawersEditable(root, "1")); // existing drawers can be removed
}

TEST(InnerDrawerCommand, BehindDoorPairUndoRedoAndRefusals) {
    Session s;
    ASSERT_TRUE(s.newFromTemplate(TemplateKind::BaseCabinet).ok());
    ASSERT_TRUE(innerDrawersEditable(cabinet(s).interior, ""));
    InnerDrawerInput in;
    in.enabled = true;

    // With the adjustable shelf still there the engine refuses; nothing changes.
    auto withShelf = innerDrawersCommand(cabinet(s), "cab1", "", in);
    ASSERT_TRUE(withShelf.ok());
    const auto undo = s.document().undoLabel();
    auto refused = s.execute(withShelf.value());
    ASSERT_FALSE(refused.ok());
    EXPECT_FALSE(refused.error().message.empty());
    EXPECT_FALSE(cabinet(s).interior.innerDrawers.has_value());
    EXPECT_EQ(s.document().undoLabel(), undo);

    ASSERT_TRUE(s.execute(project::commands::setShelfCount("cab1", "", 0)).ok());
    auto cmd = innerDrawersCommand(cabinet(s), "cab1", "", in);
    ASSERT_TRUE(cmd.ok());
    auto r = s.execute(cmd.value());
    ASSERT_TRUE(r.ok()) << r.error().message;
    ASSERT_TRUE(cabinet(s).interior.innerDrawers.has_value());
    // 2 drawers x (2 sides + box front + back + bottom); no facias with the Box front.
    EXPECT_EQ(innerDrawerParts(s, ".side."), 4);
    EXPECT_EQ(innerDrawerParts(s, ".bottom"), 2);
    EXPECT_EQ(innerDrawerParts(s, ".facia"), 0);
    // Door pair: hinges on both sides -> packers on both sides of each drawer.
    EXPECT_GE(innerDrawerParts(s, ".packer.left."), 2);
    EXPECT_GE(innerDrawerParts(s, ".packer.right."), 2);
    const auto rows = sectionRows(cabinet(s).interior);
    EXPECT_EQ(contentText(rows.at(0)), QStringLiteral("inner drawers: 2; door pair"));

    // Findings are exactly the item's furniture.inner_drawers.* Warning/Error diagnostics.
    const auto findings = innerDrawerFindings(*s.resolved(), "cab1");
    std::size_t expected = 0;
    for (const auto& d : s.resolved()->items.at(0).diagnostics)
        expected += d.severity != core::Severity::Info && d.code.rfind("furniture.inner_drawers.", 0) == 0;
    EXPECT_EQ(findings.size(), expected);
    for (const auto& d : findings) EXPECT_FALSE(d.message.empty());
    EXPECT_TRUE(innerDrawerFindings(*s.resolved(), "nope").empty());

    // Facia front: one facia per drawer.
    in.front = furniture::InnerDrawerFront::Facia;
    in.handle = furniture::InnerFaciaHandle::None;
    auto facia = s.execute(innerDrawersCommand(cabinet(s), "cab1", "", in).value());
    ASSERT_TRUE(facia.ok()) << facia.error().message;
    EXPECT_EQ(innerDrawerParts(s, ".facia"), 2);

    // Undo restores the box-front drawers, a second undo removes them; redo brings them back.
    ASSERT_TRUE(s.undo().ok());
    EXPECT_EQ(innerDrawerParts(s, ".facia"), 0);
    ASSERT_TRUE(s.undo().ok());
    EXPECT_FALSE(cabinet(s).interior.innerDrawers.has_value());
    EXPECT_EQ(innerDrawerParts(s, ".side."), 0);
    ASSERT_TRUE(s.redo().ok());
    EXPECT_EQ(innerDrawerParts(s, ".side."), 4);

    // Disabling removes them in one command; split / missing sections are refused up front.
    InnerDrawerInput off;
    ASSERT_TRUE(s.execute(innerDrawersCommand(cabinet(s), "cab1", "", off).value()).ok());
    EXPECT_FALSE(cabinet(s).interior.innerDrawers.has_value());
    EXPECT_FALSE(innerDrawersCommand(cabinet(s), "cab1", "7", in).ok());
    ASSERT_TRUE(s.execute(splitCommand("cab1", "", furniture::SplitDirection::Horizontal, 2)).ok());
    EXPECT_FALSE(innerDrawersCommand(cabinet(s), "cab1", "", in).ok());
}

} // namespace
} // namespace fos::app::test
