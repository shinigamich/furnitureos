// SPDX-License-Identifier: AGPL-3.0-only
// Editing API: atomic edits, invalid edits leave the definition unchanged, pinned parameters.
#include "test_support.hpp"

namespace fos::furniture::test {
using ::testing::Contains;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::SizeIs;

namespace {
/// Everything a definition holds, as text (CabinetDefinition has no operator==).
std::string snapshot(const CabinetDefinition& d) {
    std::ostringstream s;
    s.precision(12);
    s << d.id.str() << "|" << d.name << "\n";
    for (const auto& p : d.parameters.definitions())
        s << p.name << "=" << (p.isExpression() ? p.expression : std::to_string(p.value)) << "\n";
    for (const auto& c : d.parameters.constraints()) s << c.name << ":" << c.expression << "\n";
    for (const auto& p : d.pinnedParameters) s << "pinned " << p << "\n";
    return s.str();
}
void expectUnchanged(const CabinetDefinition& d, const CabinetDefinition& before) {
    EXPECT_EQ(snapshot(d), snapshot(before));
    EXPECT_TRUE(d.interior == before.interior);
    EXPECT_TRUE(d.construction == before.construction);
    EXPECT_TRUE(d.materials == before.materials);
    EXPECT_TRUE(d.hardware == before.hardware);
}
} // namespace

TEST(Edit, InvalidEditsAreRejectedAndLeaveTheDefinitionUnchanged) {
    const Libraries l = libs();
    CabinetDefinition d = defaultCabinet();
    d.interior.shelfCount = 1;
    ASSERT_TRUE(resolve(d, l).ok());
    const CabinetDefinition before = d;
    const std::string generatedBefore = dump(gen(d).assembly);

    auto r = setShelfCount(d, l, "", -1);
    ASSERT_FALSE(r.ok());
    EXPECT_THAT(r.error().message, HasSubstr("shelf count -1"));
    expectUnchanged(d, before);

    r = setParameter(d, l, "no_such_parameter", 3);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, core::ErrorCode::NotFound);
    expectUnchanged(d, before);

    // width 30 -> inner width 30 - 36 = -6: the standard constraint refuses it.
    r = setParameter(d, l, "width", 30);
    ASSERT_FALSE(r.ok());
    EXPECT_THAT(r.error().message, HasSubstr("Inner width must be positive"));
    expectUnchanged(d, before);

    r = setParameterExpression(d, l, "depth", "width +");
    ASSERT_FALSE(r.ok());
    expectUnchanged(d, before);

    r = splitSection(d, l, "", SplitDirection::Vertical, {SectionSize::weight(1)});
    ASSERT_FALSE(r.ok());
    EXPECT_THAT(r.error().message, HasSubstr("at least 2 child sizes (got 1)"));
    expectUnchanged(d, before);

    r = setSectionSize(d, l, "", SectionSize::fixed(100));
    ASSERT_FALSE(r.ok());
    expectUnchanged(d, before);

    r = setShelfCount(d, l, "3", 1);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, core::ErrorCode::NotFound);
    expectUnchanged(d, before);

    r = removeParameter(d, l, "width");
    ASSERT_FALSE(r.ok());
    expectUnchanged(d, before);

    r = addParameter(d, l, parametric::ParameterDef::constant("width", 5));
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, core::ErrorCode::AlreadyExists);
    expectUnchanged(d, before);

    FrontSpec emptyStack;
    emptyStack.kind = FrontKind::DrawerStack;
    r = setFront(d, l, "", emptyStack);
    ASSERT_FALSE(r.ok());
    EXPECT_THAT(r.error().message, HasSubstr("drawer stack without any drawer front"));
    expectUnchanged(d, before);

    ConstructionOptions o;
    o.top = TopKind::Rails;
    o.topJoint = JointKind::OverSides;
    r = setConstruction(d, l, o);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, core::ErrorCode::Unsupported);
    expectUnchanged(d, before);

    MaterialAssignment m = d.materials;
    m.carcass = domain::MaterialId("mat.unknown");
    r = setMaterials(d, l, m);
    ASSERT_FALSE(r.ok());
    expectUnchanged(d, before);

    // A mutation that fails aborts a generic edit as well.
    r = applyEdit(d, l, [](CabinetDefinition& c) -> core::Result<void> {
        c.name = "changed";
        return core::makeError(core::ErrorCode::InvalidArgument, "refused");
    });
    ASSERT_FALSE(r.ok());
    expectUnchanged(d, before);

    EXPECT_EQ(dump(gen(d).assembly), generatedBefore);
}

TEST(Edit, SuccessfulEditsReportTheChangedParametersSorted) {
    const Libraries l = libs();
    CabinetDefinition d = defaultCabinet();
    ASSERT_TRUE(resolve(d, l).ok());
    auto r = setParameter(d, l, "width", 800);
    ASSERT_TRUE(r.ok()) << r.error().message;
    const auto& changed = r.value().changedParameters;
    EXPECT_TRUE(std::is_sorted(changed.begin(), changed.end()));
    EXPECT_THAT(changed, Contains("width"));
    EXPECT_THAT(changed, Contains("inner_width"));   // 800 - 36 = 764
    EXPECT_THAT(changed, Contains("back_width"));    // 800 - 4 = 796
    EXPECT_THAT(changed, ::testing::Not(Contains("height")));
    EXPECT_NEAR(r.value().generated.values.at("inner_width"), 764, 1e-9);
    // Splitting moves the leaf's shelves into the first child; the front stays on the split section.
    d.interior.shelfCount = 2;
    r = splitSection(d, l, "", SplitDirection::Vertical, {SectionSize::weight(1), SectionSize::weight(1)});
    ASSERT_TRUE(r.ok()) << r.error().message;
    EXPECT_EQ(d.interior.shelfCount, 0);
    EXPECT_EQ(d.interior.children[0].shelfCount, 2);
    EXPECT_TRUE(has(r.value().generated.assembly, "shelf.1.2"));
    EXPECT_TRUE(has(r.value().generated.assembly, "partition.1"));
}

TEST(Edit, PinnedDerivedParameterWarnsWhenTheConstructionChangesItsFormula) {
    // Reviewer scenario: pin back_width to the Overlay formula, then switch to a grooved back.
    const Libraries l = libs();
    CabinetDefinition d = defaultCabinet();
    auto pin = setParameterExpression(d, l, "back_width", "width - 2 * back_inset");
    ASSERT_TRUE(pin.ok()) << pin.error().message;
    EXPECT_THAT(errorsOf(pin.value().generated.diagnostics), IsEmpty());
    ConstructionOptions o = d.construction;
    o.back = BackMount::Grooved;
    auto changed = setConstruction(d, l, o);
    ASSERT_TRUE(changed.ok()) << changed.error().message; // a geometric conflict is a diagnostic, not a refusal
    const core::Diagnostics& ds = changed.value().generated.diagnostics;
    // Pinned: 600 - 2 * 2 = 596. Standard grooved formula: inner_width + 2 * (8 - 1) = 564 + 14 = 578.
    const auto warnings = messagesWithCode(ds, "furniture.parameter.pinned_formula_changed");
    ASSERT_THAT(warnings, SizeIs(1));
    EXPECT_THAT(warnings[0], HasSubstr("'back_width' is pinned to 'width - 2 * back_inset'"));
    EXPECT_THAT(warnings[0], HasSubstr("standard formula is 'inner_width + 2 * back_engagement'"));
    EXPECT_THAT(warnings[0], HasSubstr("pinned value 596 mm, standard value 578 mm"));
    // Self-check: back x = 18 - 7 = 11 .. 11 + 596 = 607 runs through the right side (x 582..600);
    // its groove only reaches x 582..590 -> 18 x 3 (back thickness, y 547..550) x 698 (back height
    // 684 + 14, z 11..709) outside any groove. The left side (back x 11..18 = 7 mm, inside the
    // groove x 10..18), bottom and top engagements are legitimate.
    const auto errors = messagesWithCode(ds, "furniture.geometry.interpenetration");
    ASSERT_THAT(errors, SizeIs(1));
    EXPECT_THAT(errors[0], HasSubstr("'Right side' (cab/side.right) and 'Back panel' (cab/back) interpenetrate by "
                                     "18 x 3 x 698 mm (x 582..600, y 547..550, z 11..709)"));
    EXPECT_EQ(d.construction.back, BackMount::Grooved);
    EXPECT_EQ(d.parameters.find("back_width")->expression, "width - 2 * back_inset");

    // Resetting the parameter clears both findings: back 578 wide at x 11..589.
    auto reset = resetParameter(d, l, "back_width");
    ASSERT_TRUE(reset.ok()) << reset.error().message;
    EXPECT_THAT(errorsOf(reset.value().generated.diagnostics), IsEmpty());
    expectSize(part(reset.value().generated.assembly, "back"), 578, 698, 3);
}

TEST(Edit, PinnedParameterWhoseFormulaDoesNotChangeGivesNoWarning) {
    const Libraries l = libs();
    CabinetDefinition d = defaultCabinet();
    // shelf_depth = "interior_depth - shelf_setback" under every back mount.
    ASSERT_TRUE(setParameterExpression(d, l, "shelf_depth", "depth / 2").ok());
    ConstructionOptions o = d.construction;
    o.back = BackMount::Inset;
    auto r = setConstruction(d, l, o);
    ASSERT_TRUE(r.ok()) << r.error().message;
    EXPECT_FALSE(hasCode(r.value().generated.diagnostics, "furniture.parameter.pinned_formula_changed"));
    // An edit that does not touch the construction never warns, even with pins in place.
    ASSERT_TRUE(setParameterExpression(d, l, "back_width", "inner_width - 2").ok());
    auto w = setParameter(d, l, "width", 700);
    ASSERT_TRUE(w.ok()) << w.error().message;
    EXPECT_FALSE(hasCode(w.value().generated.diagnostics, "furniture.parameter.pinned_formula_changed"));
}

TEST(Edit, EditThenUndoByCopyReproducesTheAssembly) {
    const Libraries l = libs();
    CabinetDefinition d = templates::tallCabinet(domain::AssemblyId("tall"), templateMaterials());
    const CabinetDefinition undo = d;
    const std::string original = dump(gen(d).assembly);
    ASSERT_TRUE(setShelfCount(d, l, "2", 5).ok());
    ASSERT_TRUE(setParameter(d, l, "width", 500).ok());
    EXPECT_NE(dump(gen(d).assembly), original);
    d = undo;
    EXPECT_EQ(dump(gen(d).assembly), original);
}

} // namespace fos::furniture::test
