// SPDX-License-Identifier: AGPL-3.0-only
// Interior layout: section tree splits, shelf distribution, divider joints, shelf pins.
// All cabinets use DEFAULT parameters unless stated: 600 x 720 x 560, 18 mm, overlay back,
// inner width 564 (x 18..582), interior z 18..702 (inner height 684), shelf_clearance 1.
#include "test_support.hpp"

namespace fos::furniture::test {
using domain::Face;
using domain::Hole;
using ::testing::ElementsAre;
using ::testing::HasSubstr;
using ::testing::IsEmpty;

namespace {
Section split(SplitDirection direction, std::vector<Section> children) {
    Section s;
    s.split = direction;
    s.children = std::move(children);
    return s;
}
const domain::HardwareInstance& inst(const domain::Assembly& a, const std::string& suffix) {
    static const domain::HardwareInstance missing;
    const auto* h = instance(a, suffix);
    EXPECT_NE(h, nullptr) << suffix;
    return h ? *h : missing;
}
} // namespace

TEST(Interior, VerticalSplitWithFixedExpressionAndWeightSizes) {
    CabinetDefinition d = defaultCabinet();
    d.interior = split(SplitDirection::Vertical, {sized(SectionSize::fixed(150)), sized(SectionSize::expr("inner_width / 4")),
                                                  sized(SectionSize::weight(1))});
    const GenerationResult r = gen(d);
    // Available 564 - 2 * 18 = 528; fixed 150; expression 564 / 4 = 141; weight gets 528 - 291 = 237.
    // Child 1 x 18..168, partition.1 168..186, child 2 186..327, partition.2 327..345, child 3 345..582.
    // Partitions: inner height 684 long, interior depth 560 wide (overlay back), z 18..702.
    expectSize(part(r.assembly, "partition.1"), 684, 560, 18);
    expectBox(part(r.assembly, "partition.1"), {168, 0, 18}, {186, 560, 702});
    expectBox(part(r.assembly, "partition.2"), {327, 0, 18}, {345, 560, 702});
    EXPECT_THAT(errorsOf(r.diagnostics), IsEmpty());
    EXPECT_THAT(interpenetrations(r.assembly), IsEmpty());
}

TEST(Interior, HorizontalSplitByWeightPlacesTheFixedShelf) {
    CabinetDefinition d = defaultCabinet();
    d.interior = split(SplitDirection::Horizontal, {sized(SectionSize::weight(1)), sized(SectionSize::weight(2))});
    const GenerationResult r = gen(d);
    // Available 684 - 18 = 666 -> 222 (z 18..240) and 444 (z 258..702); fixed shelf z 240..258,
    // full inner width and full interior depth.
    expectSize(part(r.assembly, "fixed_shelf.1"), 564, 560, 18);
    expectBox(part(r.assembly, "fixed_shelf.1"), {18, 0, 240}, {582, 560, 258});
    // Fixed shelves to OUTER sides keep the selected confirmats: through holes from the left
    // side's outer face A at u = 249 (mid-plane z 240 + 9), v = 50 / 280 / 510.
    EXPECT_THAT(holes(r.assembly, "side.left", "hw.joint.fixed_shelf.1.side.left"),
                ElementsAre(Hole{Face::A, 249, 50, 7, 0, true}, Hole{Face::A, 249, 280, 7, 0, true},
                            Hole{Face::A, 249, 510, 7, 0, true}));
    EXPECT_THAT(errorsOf(r.diagnostics), IsEmpty());
}

TEST(Interior, FixedSizesThatDoNotFitAreRejectedWithTheNumbers) {
    const Libraries l = libs();
    CabinetDefinition d = defaultCabinet();
    // 300 + 300 fixed + a weight in 564 - 2 * 18 = 528 mm: 528 - 600 = -72 left for the weight.
    d.interior = split(SplitDirection::Vertical, {sized(SectionSize::fixed(300)), sized(SectionSize::fixed(300)),
                                                  sized(SectionSize::weight(1))});
    auto r = generate(d, l);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, core::ErrorCode::ConstraintViolation);
    EXPECT_THAT(r.error().message, HasSubstr("Section 'root'"));
    EXPECT_THAT(r.error().message, HasSubstr("add up to 600 mm but only 528 mm are available, leaving -72 mm"));

    // Only fixed sizes, too much: one partition -> 546 available; 300 + 300 = 600 -> 54 too much.
    d.interior = split(SplitDirection::Vertical, {sized(SectionSize::fixed(300)), sized(SectionSize::fixed(300))});
    r = generate(d, l);
    ASSERT_FALSE(r.ok());
    EXPECT_THAT(r.error().message, HasSubstr("600 mm but only 546 mm are available (54 mm too much)"));

    // Only fixed sizes, too little: 200 + 200 = 400 of 546 -> 146 unassigned.
    d.interior = split(SplitDirection::Vertical, {sized(SectionSize::fixed(200)), sized(SectionSize::fixed(200))});
    r = generate(d, l);
    ASSERT_FALSE(r.ok());
    EXPECT_THAT(r.error().message, HasSubstr("leaving 146 mm of the available 546 mm unassigned"));

    // A weight of 0 and an expression that is not positive name the entry.
    d.interior = split(SplitDirection::Vertical, {sized(SectionSize::weight(0)), sized(SectionSize::weight(1))});
    r = generate(d, l);
    ASSERT_FALSE(r.ok());
    EXPECT_THAT(r.error().message, HasSubstr("entry 1: the weight must be greater than 0 (is 0)"));
    d.interior = split(SplitDirection::Vertical, {sized(SectionSize::expr("width - 700")), sized(SectionSize::weight(1))});
    r = generate(d, l);
    ASSERT_FALSE(r.ok());
    EXPECT_THAT(r.error().message, HasSubstr("entry 1: the size must be greater than 0 mm (is -100 mm)"));

    // Structural errors: one child only; a leaf with children; too many shelves (40 x 18 = 720 > 684).
    d.interior = split(SplitDirection::Vertical, {sized(SectionSize::weight(1))});
    r = generate(d, l);
    ASSERT_FALSE(r.ok());
    EXPECT_THAT(r.error().message, HasSubstr("has 1 child section(s); a split needs at least 2"));
    d.interior = Section{};
    d.interior.shelfCount = 40;
    r = generate(d, l);
    ASSERT_FALSE(r.ok());
    EXPECT_THAT(r.error().message, HasSubstr("is 684 mm high; 40 shelves of 18 mm need more than 720 mm"));
}

TEST(Interior, ShelvesAreEvenlyDistributedAtDefaultClearance) {
    CabinetDefinition d = defaultCabinet();
    d.interior.shelfCount = 2;
    const GenerationResult r = gen(d);
    // 2 shelves in 684: clear space (684 - 36) / 3 = 216 -> z 234..252 and 468..486.
    // Length 564 - 2 * 1 = 562 (x 19..581); depth = interior depth 560 - setback 20 = 540 (y 20..560).
    expectSize(part(r.assembly, "shelf.1"), 562, 540, 18);
    expectBox(part(r.assembly, "shelf.1"), {19, 20, 234}, {581, 560, 252});
    expectBox(part(r.assembly, "shelf.2"), {19, 20, 468}, {581, 560, 486});
    // Pins although the shelf is 1 mm away from the sides: centre z = 234 - 2.5 = 231.5,
    // y = 20 + 50 = 70 and 560 - 50 = 510; outer sides keep the catalogue depth 13.
    EXPECT_THAT(holes(r.assembly, "side.left", "hw.pins.shelf.1"),
                ElementsAre(Hole{Face::B, 231.5, 70, 5, 13, false}, Hole{Face::B, 231.5, 510, 5, 13, false}));
    EXPECT_THAT(holes(r.assembly, "side.right", "hw.pins.shelf.2"),
                ElementsAre(Hole{Face::A, 465.5, 70, 5, 13, false}, Hole{Face::A, 465.5, 510, 5, 13, false}));
    EXPECT_THAT(errorsOf(r.diagnostics), IsEmpty());
    EXPECT_THAT(interpenetrations(r.assembly), IsEmpty());
}

TEST(Interior, ShelfIdsAreStableWhenTheShelfCountChanges) {
    const Libraries l = libs();
    CabinetDefinition d = defaultCabinet();
    d.interior = split(SplitDirection::Vertical, {sized(SectionSize::weight(1), 2), sized(SectionSize::weight(1), 1)});
    const GenerationResult before = gen(d);
    ASSERT_TRUE(has(before.assembly, "shelf.1.2"));
    const std::string siblingBefore = dump([&] {
        domain::Assembly a;
        a.parts = {part(before.assembly, "shelf.2.1")};
        return a;
    }());

    auto more = setShelfCount(d, l, "1", 3);
    ASSERT_TRUE(more.ok()) << more.error().message;
    const domain::Assembly& a = more.value().generated.assembly;
    for (const char* slot : {"shelf.1.1", "shelf.1.2", "shelf.1.3", "shelf.2.1"}) EXPECT_TRUE(has(a, slot)) << slot;
    EXPECT_NE(instance(a, "hw.pins.shelf.1.3"), nullptr);
    // The sibling section's shelf is untouched (same id, geometry and pin holes).
    domain::Assembly sibling;
    sibling.parts = {part(a, "shelf.2.1")};
    EXPECT_EQ(dump(sibling), siblingBefore);
    // Column 1: 3 shelves in 684 -> space (684 - 54) / 4 = 157.5; shelf.1.3 z = 18 + 3 * 157.5 + 36 = 526.5.
    EXPECT_NEAR(part(a, "shelf.1.3").bounds().min.z, 526.5, 1e-9);

    auto fewer = setShelfCount(d, l, "1", 1);
    ASSERT_TRUE(fewer.ok()) << fewer.error().message;
    EXPECT_TRUE(has(fewer.value().generated.assembly, "shelf.1.1"));
    EXPECT_FALSE(has(fewer.value().generated.assembly, "shelf.1.2"));
    EXPECT_EQ(instance(fewer.value().generated.assembly, "hw.pins.shelf.1.2"), nullptr);
    EXPECT_THAT(slots(fewer.value().generated.assembly), ::testing::Contains("shelf.2.1"));
}

TEST(Interior, ShelfPinDepthIsLimitedOnlyOnTheSharedSupport) {
    CabinetDefinition d = defaultCabinet();
    d.materials.carcass = domain::MaterialId("mat.pb16");
    d.interior = split(SplitDirection::Vertical, {sized(SectionSize::weight(1), 1), sized(SectionSize::weight(1))});
    const GenerationResult r = gen(d);
    // 16 mm: inner width 600 - 32 = 568, available 568 - 16 = 552 -> 276 each:
    // column 1 x 16..292, partition.1 x 292..308 (its face A at x = 292 faces column 1).
    // Inner height 720 - 32 = 688 (z 16..704); shelf z = 16 + (688 - 16) / 2 = 352; pin u = 352 - 2.5 - 16 = 333.5
    // on the partition (its local x starts at z 16), 349.5 on the side (starts at z 0).
    // Outer side: catalogue depth 13 (13 < 16 - 2 leaves the required material).
    // Partition: shared-panel limit (16 - 2) / 2 = 7.
    EXPECT_THAT(holes(r.assembly, "side.left", "hw.pins.shelf.1.1"),
                ElementsAre(Hole{Face::B, 349.5, 70, 5, 13, false}, Hole{Face::B, 349.5, 510, 5, 13, false}));
    EXPECT_THAT(holes(r.assembly, "partition.1", "hw.pins.shelf.1.1"),
                ElementsAre(Hole{Face::A, 333.5, 70, 5, 7, false}, Hole{Face::A, 333.5, 510, 5, 7, false}));
    EXPECT_EQ(inst(r.assembly, "hw.pins.shelf.1.1").quantity, 4);
    EXPECT_THAT(errorsOf(r.diagnostics), IsEmpty());
}

TEST(Interior, FixedShelvesOnBothSidesOfAPartitionGetBlindDowelsThatDoNotCollide) {
    // Reviewer scenario: width 600, two columns, each split in two by weight.
    CabinetDefinition d = defaultCabinet();
    const Section column = split(SplitDirection::Horizontal, {sized(SectionSize::weight(1)), sized(SectionSize::weight(1))});
    Section c1 = column, c2 = column;
    d.interior = split(SplitDirection::Vertical, {c1, c2});
    const GenerationResult r = gen(d);
    const domain::Assembly& a = r.assembly;
    // Columns: available 564 - 18 = 546 -> 273: column 1 x 18..291, partition.1 x 291..309, column 2 x 309..582.
    // Each column: available 684 - 18 = 666 -> 333: fixed shelves z 351..369 on both sides of the partition.
    expectBox(part(a, "fixed_shelf.1.1"), {18, 0, 351}, {291, 560, 369});
    expectBox(part(a, "fixed_shelf.2.1"), {309, 0, 351}, {582, 560, 369});
    // Shared-panel rule: both joints into the partition are dowels with blind face holes of
    // min(12, (18 - 2) / 2) = 8 mm - from face A (x = 291, column 1) and face B (x = 309, column 2)
    // at the same spot: u = 360 - 18 = 342 (mid-plane above the partition's lower end),
    // v = 50 / 280 / 510 (560 deep joint, end offset 50, 3 fasteners). 8 + 8 = 16 < 18: no collision.
    EXPECT_THAT(holes(a, "partition.1", "hw.joint.fixed_shelf.1.1.partition.1"),
                ElementsAre(Hole{Face::A, 342, 50, 8, 8, false}, Hole{Face::A, 342, 280, 8, 8, false},
                            Hole{Face::A, 342, 510, 8, 8, false}));
    EXPECT_THAT(holes(a, "partition.1", "hw.joint.fixed_shelf.2.1.partition.1"),
                ElementsAre(Hole{Face::B, 342, 50, 8, 8, false}, Hole{Face::B, 342, 280, 8, 8, false},
                            Hole{Face::B, 342, 510, 8, 8, false}));
    // The edge holes are deepened so that 8 + 22 = 30 takes the whole 30 mm dowel
    // (fixed_shelf.1.1 ends at the partition with W2: u = local y, v = mid-thickness 9).
    EXPECT_THAT(holes(a, "fixed_shelf.1.1", "hw.joint.fixed_shelf.1.1.partition.1"),
                ElementsAre(Hole{Face::W2, 50, 9, 8, 22, false}, Hole{Face::W2, 280, 9, 8, 22, false},
                            Hole{Face::W2, 510, 9, 8, 22, false}));
    EXPECT_EQ(inst(a, "hw.joint.fixed_shelf.1.1.partition.1").definition.str(), "hw.dowel_8x30");
    EXPECT_EQ(inst(a, "hw.joint.fixed_shelf.2.1.partition.1").definition.str(), "hw.dowel_8x30");
    // Joints to the outer sides keep the selected confirmat.
    EXPECT_EQ(inst(a, "hw.joint.fixed_shelf.1.1.side.left").definition.str(), "hw.confirmat_7x50");
    EXPECT_EQ(inst(a, "hw.joint.fixed_shelf.2.1.side.right").definition.str(), "hw.confirmat_7x50");
    EXPECT_THAT(errorsOf(machining::validateOperations(part(a, "partition.1"))), IsEmpty());
    EXPECT_FALSE(hasCode(r.diagnostics, "machining.hole.collision"));
    EXPECT_THAT(errorsOf(r.diagnostics), IsEmpty());
    EXPECT_THAT(interpenetrations(a), IsEmpty());
}

TEST(Interior, PartitionsAboveAndBelowAFixedShelfGetBlindDowels) {
    // Transposed case: two rows, each split in two columns -> partitions meet the fixed shelf
    // from below (face B) and above (face A) at the same x.
    CabinetDefinition d = defaultCabinet();
    const Section row = split(SplitDirection::Vertical, {sized(SectionSize::weight(1)), sized(SectionSize::weight(1))});
    d.interior = split(SplitDirection::Horizontal, {row, row});
    const GenerationResult r = gen(d);
    // Rows: 666 / 2 = 333 -> fixed_shelf.1 z 351..369 (x 18..582); partitions x 291..309 in both rows.
    // Dowel face holes in the fixed shelf at u = 300 - 18 = 282 (partition mid-plane), v = 50 / 280 / 510, 8 deep.
    EXPECT_THAT(holes(r.assembly, "fixed_shelf.1", "hw.joint.partition.1.1.fixed_shelf.1"),
                ElementsAre(Hole{Face::B, 282, 50, 8, 8, false}, Hole{Face::B, 282, 280, 8, 8, false},
                            Hole{Face::B, 282, 510, 8, 8, false}));
    EXPECT_THAT(holes(r.assembly, "fixed_shelf.1", "hw.joint.partition.2.1.fixed_shelf.1"),
                ElementsAre(Hole{Face::A, 282, 50, 8, 8, false}, Hole{Face::A, 282, 280, 8, 8, false},
                            Hole{Face::A, 282, 510, 8, 8, false}));
    // Partition to the outer bottom / top keeps confirmats.
    EXPECT_EQ(inst(r.assembly, "hw.joint.partition.1.1.bottom").definition.str(), "hw.confirmat_7x50");
    EXPECT_THAT(errorsOf(r.diagnostics), IsEmpty());
}

} // namespace fos::furniture::test
