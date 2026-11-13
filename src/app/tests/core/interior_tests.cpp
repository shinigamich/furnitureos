// SPDX-License-Identifier: AGPL-3.0-only
// Interior editor view-model: section rows, size syntax, split / remove split / size / shelf
// count / front commands executed through the Session, refusals and undo / redo.
//
// Base cabinet template (furniture templates.cpp): width 600, height 820, depth 560,
// 18 mm carcass, sides to the floor, plinth 100, rails on top, overlay back (interior depth =
// depth = 560), one shelf, door pair. Hand arithmetic used below:
//   inner width  = 600 - 2 * 18                    = 564
//   inner height = 820 - 100 (plinth) - 18 - 18    = 684
//   shelf length = opening - 2 * shelf_clearance(1); shelf depth = 560 - 20 (setback) = 540
//   front width  = 564 + 2 * overlay_outer(18 - 2)  = 596; height 684 + 2 * 16 = 716
#include "fos/app/command_text.hpp"
#include "fos/app/interior_rows.hpp"
#include "test_support.hpp"

namespace fos::app::test {
namespace {

using furniture::SizeKind;
using furniture::SplitDirection;

const furniture::CabinetDefinition& cabinet(const Session& s) { return *s.project().items.at(0).cabinet(); }
std::size_t partCount(const Session& s) { return s.resolved()->model.assemblies.at(0).parts.size(); }

struct Fixture : ::testing::Test {
    Session s;
    void SetUp() override { ASSERT_TRUE(s.newFromTemplate(TemplateKind::BaseCabinet).ok()); }
    /// Executes and asserts success.
    void run(const project::Command& c) {
        auto r = s.execute(c);
        ASSERT_TRUE(r.ok()) << r.error().message;
    }
    void run(const core::Result<project::Command>& c) {
        ASSERT_TRUE(c.ok()) << c.error().message;
        run(c.value());
    }
    /// Executes a command that must be refused; the interior and the undo stack stay as they were.
    void refused(const project::Command& c) {
        const furniture::Section before = cabinet(s).interior;
        const auto undo = s.document().undoLabel();
        const auto revision = s.revision();
        auto r = s.execute(c);
        ASSERT_FALSE(r.ok());
        EXPECT_FALSE(r.error().message.empty());
        EXPECT_EQ(cabinet(s).interior, before);
        EXPECT_EQ(s.document().undoLabel(), undo);
        EXPECT_EQ(s.revision(), revision);
    }
};

TEST(SectionSize, Syntax) {
    auto f = parseSectionSize(QStringLiteral(" 300 mm "));
    ASSERT_TRUE(f.ok());
    EXPECT_EQ(f.value(), furniture::SectionSize::fixed(300));
    auto w = parseSectionSize(QStringLiteral("2,5*"));
    ASSERT_TRUE(w.ok());
    EXPECT_EQ(w.value(), furniture::SectionSize::weight(2.5));
    EXPECT_EQ(parseSectionSize(QStringLiteral("*")).value(), furniture::SectionSize::weight(1));
    auto e = parseSectionSize(QStringLiteral("width / 4"));
    ASSERT_TRUE(e.ok());
    EXPECT_EQ(e.value(), furniture::SectionSize::expr("width / 4"));
    EXPECT_FALSE(parseSectionSize(QStringLiteral("  ")).ok());
    EXPECT_FALSE(parseSectionSize(QStringLiteral("abc*")).ok());
    // Inverse formatting.
    EXPECT_EQ(sizeText(furniture::SectionSize::fixed(140)), QStringLiteral("140"));
    EXPECT_EQ(sizeText(furniture::SectionSize::weight(2)), QStringLiteral("2*"));
    EXPECT_EQ(sizeText(furniture::SectionSize::expr("height / 3")), QStringLiteral("height / 3"));
    auto list = parseSizeList(QStringLiteral("140; 1*;2*"));
    ASSERT_TRUE(list.ok());
    ASSERT_EQ(list.value().size(), 3u);
    EXPECT_EQ(list.value()[0].kind, SizeKind::Fixed);
    EXPECT_EQ(list.value()[2], furniture::SectionSize::weight(2));
    EXPECT_EQ(sizeListText(list.value()), QStringLiteral("140; 1*; 2*"));
    EXPECT_FALSE(parseSizeList(QStringLiteral("140;;1*")).ok());
    EXPECT_EQ(sizeDescription(furniture::SectionSize::fixed(300)), QStringLiteral("300 mm (fixed)"));
    EXPECT_EQ(sizeDescription(furniture::SectionSize::weight(1)), QStringLiteral("share 1"));
}

TEST_F(Fixture, RowsOfTheTemplate) {
    const auto rows = sectionRows(cabinet(s).interior);
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_TRUE(rows[0].isRoot());
    EXPECT_TRUE(rows[0].isLeaf());
    EXPECT_EQ(rows[0].shelfCount, 1);
    EXPECT_EQ(sectionCaption(rows[0]), QStringLiteral("Whole interior"));
    EXPECT_EQ(contentText(rows[0]), QStringLiteral("shelves: 1; door pair"));
}

TEST_F(Fixture, SplitVerticallyThenShelvesThenUndoRedo) {
    const furniture::Section original = cabinet(s).interior;
    const auto split = splitCommand("cab1", "", SplitDirection::Vertical, 2);
    EXPECT_EQ(commandText(split.label, s.project()), QStringLiteral("Split the whole interior of Base cabinet into 2"));
    run(split);
    const auto rows = sectionRows(cabinet(s).interior);
    ASSERT_EQ(rows.size(), 3u);
    EXPECT_EQ(rows[1].path, "1");
    EXPECT_EQ(rows[2].path, "2");
    EXPECT_EQ(sectionCaption(rows[1]), QStringLiteral("Section 1 (left)"));
    EXPECT_EQ(sectionCaption(rows[2]), QStringLiteral("Section 2 (right)"));
    EXPECT_EQ(contentText(rows[0]), QStringLiteral("split vertically into 2; door pair"));
    // Two equal shares of 564 - 18 (partition) = 546 -> 273 each. The leaf's shelf moved into
    // section 1: 273 - 2 * 1 = 271 long, 540 deep.
    const auto* partition = part(s, "cab1/partition.1");
    ASSERT_NE(partition, nullptr);
    EXPECT_DOUBLE_EQ(partition->thickness, 18);
    EXPECT_DOUBLE_EQ(partition->length, 684); // between bottom and rails
    const auto* shelf = part(s, "cab1/shelf.1.1");
    ASSERT_NE(shelf, nullptr);
    EXPECT_DOUBLE_EQ(shelf->length, 271);
    EXPECT_DOUBLE_EQ(shelf->width, 540);
    EXPECT_EQ(part(s, "cab1/shelf.1"), nullptr);

    run(project::commands::setShelfCount("cab1", "1", 2));
    EXPECT_NE(part(s, "cab1/shelf.1.2"), nullptr);
    EXPECT_DOUBLE_EQ(part(s, "cab1/shelf.1.2")->length, 271);
    EXPECT_EQ(part(s, "cab1/shelf.2.1"), nullptr);
    // 2 sides, bottom, 2 rails, plinth, back, partition, 2 shelves, 2 doors.
    EXPECT_EQ(partCount(s), 12u);

    ASSERT_TRUE(s.undo().ok());
    ASSERT_TRUE(s.undo().ok());
    EXPECT_EQ(cabinet(s).interior, original);
    EXPECT_DOUBLE_EQ(part(s, "cab1/shelf.1")->length, 562); // 564 - 2
    EXPECT_EQ(partCount(s), 10u);
    ASSERT_TRUE(s.redo().ok());
    ASSERT_TRUE(s.redo().ok());
    EXPECT_EQ(partCount(s), 12u);
}

TEST_F(Fixture, SectionSizesFixedWeightExpression) {
    run(splitCommand("cab1", "", SplitDirection::Vertical, 2));
    // Fixed 200: section 1 shelf 200 - 2 = 198.
    run(sectionSizeCommand("cab1", "1", QStringLiteral("200")));
    EXPECT_DOUBLE_EQ(part(s, "cab1/shelf.1.1")->length, 198);
    EXPECT_EQ(sizeDescription(sectionRows(cabinet(s).interior)[1].size), QStringLiteral("200 mm (fixed)"));
    // Weights 2 : 1 of 546 -> 364 : 182; shelf 362.
    run(sectionSizeCommand("cab1", "1", QStringLiteral("2*")));
    EXPECT_DOUBLE_EQ(part(s, "cab1/shelf.1.1")->length, 362);
    // Expression width / 4 = 150; shelf 148.
    run(sectionSizeCommand("cab1", "1", QStringLiteral("width / 4")));
    EXPECT_DOUBLE_EQ(part(s, "cab1/shelf.1.1")->length, 148);
    EXPECT_EQ(sizeText(sectionRows(cabinet(s).interior)[1].size), QStringLiteral("width / 4"));
}

TEST_F(Fixture, RefusedEditsChangeNothing) {
    run(splitCommand("cab1", "", SplitDirection::Vertical, 2));
    // 600 mm fixed cannot fit the 546 mm available for the children.
    refused(sectionSizeCommand("cab1", "1", QStringLiteral("600")).value());
    refused(sectionSizeCommand("cab1", "1", QStringLiteral("no_such_parameter + 1")).value());
    refused(sectionSizeCommand("cab1", "1", QStringLiteral("-5")).value());
    refused(project::commands::setSectionSize("cab1", "", furniture::SectionSize::fixed(100))); // root has no size
    refused(project::commands::setShelfCount("cab1", "1", -1));
    refused(project::commands::setShelfCount("cab1", "", 2)); // the root is split now
    refused(splitCommand("cab1", "1", SplitDirection::Horizontal, 1)); // at least 2 children
    refused(splitCommand("cab1", "", SplitDirection::Horizontal, 2)); // already split
    refused(splitCommand("cab1", "7", SplitDirection::Horizontal, 2)); // no such section
    // Input errors never reach the document.
    EXPECT_FALSE(sectionSizeCommand("cab1", "1", QStringLiteral("")).ok());
    EXPECT_FALSE(removeSplitCommand(cabinet(s), "cab1", "1").ok()); // a leaf
    EXPECT_FALSE(removeSplitCommand(cabinet(s), "cab1", "9").ok());
}

TEST_F(Fixture, RemoveSplitAndClearInterior) {
    run(splitCommand("cab1", "", SplitDirection::Horizontal, 3));
    EXPECT_EQ(sectionCaption(sectionRows(cabinet(s).interior)[1]), QStringLiteral("Section 1 (bottom)"));
    EXPECT_EQ(sectionCaption(sectionRows(cabinet(s).interior)[2]), QStringLiteral("Section 2"));
    EXPECT_EQ(sectionCaption(sectionRows(cabinet(s).interior)[3]), QStringLiteral("Section 3 (top)"));
    EXPECT_NE(part(s, "cab1/fixed_shelf.2"), nullptr); // second divider of the root
    run(removeSplitCommand(cabinet(s), "cab1", ""));
    const auto rows = sectionRows(cabinet(s).interior);
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0].shelfCount, 0);
    ASSERT_TRUE(rows[0].front.has_value()); // the front is kept
    EXPECT_EQ(rows[0].front->kind, furniture::FrontKind::DoorPair);
    EXPECT_EQ(part(s, "cab1/fixed_shelf.1"), nullptr);

    run(clearInteriorCommand("cab1"));
    EXPECT_EQ(cabinet(s).interior, furniture::Section{});
    // 2 sides, bottom, 2 rails, plinth, back; no shelf, no doors.
    EXPECT_EQ(partCount(s), 7u);
}

TEST_F(Fixture, FrontsDoorDrawersNone) {
    FrontInput in = frontInput(cabinet(s).interior.front);
    EXPECT_EQ(in.choice, FrontInput::Choice::DoorPair);
    EXPECT_EQ(in.handle, furniture::HandlePosition::Top);

    // Single door hinged right: 596 x 716 (see header).
    in.choice = FrontInput::Choice::Door;
    in.hingeSide = furniture::HingeSide::Right;
    run(frontCommand("cab1", "", in));
    const auto* door = part(s, "cab1/door");
    ASSERT_NE(door, nullptr);
    EXPECT_DOUBLE_EQ(door->length, 716);
    EXPECT_DOUBLE_EQ(door->width, 596);
    EXPECT_EQ(part(s, "cab1/door.left"), nullptr);
    EXPECT_EQ(contentText(sectionRows(cabinet(s).interior)[0]), QStringLiteral("shelves: 1; door, hinged right"));

    // Drawer stack 140 + two equal shares of the rest: 716 - 2 gaps of 3 = 710; 710 - 140 = 570
    // -> 285 each. Drawer fronts: length along x = 596, width = height.
    in.choice = FrontInput::Choice::DrawerStack;
    in.drawerHeights = QStringLiteral("140; 1*; 1*");
    run(frontCommand("cab1", "", in));
    ASSERT_NE(part(s, "cab1/drawer.3"), nullptr);
    EXPECT_DOUBLE_EQ(part(s, "cab1/drawer.1")->width, 140);
    EXPECT_DOUBLE_EQ(part(s, "cab1/drawer.2")->width, 285);
    EXPECT_DOUBLE_EQ(part(s, "cab1/drawer.3")->width, 285);
    EXPECT_DOUBLE_EQ(part(s, "cab1/drawer.1")->length, 596);
    EXPECT_EQ(frontInput(cabinet(s).interior.front).drawerHeights, QStringLiteral("140; 1*; 1*"));

    // Refusals: a parse error yields no command; too tall a drawer is refused by the engine.
    in.drawerHeights = QStringLiteral("140;");
    EXPECT_FALSE(frontCommand("cab1", "", in).ok());
    in.drawerHeights = QStringLiteral("900; 1*");
    refused(frontCommand("cab1", "", in).value());

    in.choice = FrontInput::Choice::None;
    const auto none = frontCommand("cab1", "", in);
    ASSERT_TRUE(none.ok());
    EXPECT_EQ(commandText(none.value().label, s.project()), QStringLiteral("Remove front of the whole interior of Base cabinet"));
    run(none);
    EXPECT_FALSE(cabinet(s).interior.front.has_value());
    EXPECT_EQ(partCount(s), 8u); // 10 - 2 doors
    // Undo three times: the door pair is back.
    for (int i = 0; i < 3; ++i) ASSERT_TRUE(s.undo().ok());
    EXPECT_NE(part(s, "cab1/door.left"), nullptr);
    EXPECT_DOUBLE_EQ(part(s, "cab1/door.left")->width, 296.5); // (596 - 3) / 2
}

} // namespace
} // namespace fos::app::test
