// SPDX-License-Identifier: AGPL-3.0-only
// Wall mounting: HardwareSelection::hanger -> two cabinet hangers and a wall rail.
#include "test_support.hpp"

namespace fos::furniture::test {
using ::testing::HasSubstr;
using ::testing::IsEmpty;

namespace {
/// Wall cabinet 600 x 720 x 320, top / bottom between the sides, Grooved back with
/// groove_distance `gd`, two shelves, generic hanger selected.
CabinetDefinition hungCabinet(BackMount back = BackMount::Grooved, double gd = 18) {
    ConstructionOptions o;
    o.back = back;
    CabinetDefinition d = defaultCabinet(o);
    parametric::Transaction tx;
    tx.setValue("depth", 320).setValue("groove_distance", gd);
    EXPECT_TRUE(d.parameters.apply(tx).ok());
    d.interior.shelfCount = 2;
    d.hardware.hanger = domain::HardwareId(hardware::ids::kHangerGeneric);
    return d;
}
void expectHole(const domain::Hole& h, domain::Face face, double u, double v) {
    EXPECT_EQ(h.face, face);
    EXPECT_NEAR(h.u, u, 1e-9);
    EXPECT_NEAR(h.v, v, 1e-9);
    EXPECT_NEAR(h.diameter, 3, 1e-9);
    EXPECT_NEAR(h.depth, 10, 1e-9);
    EXPECT_FALSE(h.through);
}
} // namespace

TEST(Hanger, DefaultSelectionIsEmptyAndPlacesNothing) {
    EXPECT_TRUE(HardwareSelection{}.hanger.empty());
    const GenerationResult r = gen(defaultCabinet());
    for (const auto& h : r.assembly.hardware) {
        EXPECT_NE(h.definition.str(), hardware::ids::kHangerGeneric);
        EXPECT_NE(h.definition.str(), hardware::ids::kWallRailGeneric);
    }
}

// Sides x 0..18 / 582..600, y 0..320, z 0..720; local x = asm z, local y = asm y; inner faces
// left B, right A. Top underside (reference) z = 720 - 18 = 702.
//   screws: y = 320 - 8 = 312; z = 702 - 15 = 687 and 687 - 32 = 655.
//   rail: x 0..600 -> 600 mm, top edge z = 702 - 20 = 682 on the wall plane y = 320.
//   back: rear face y = 320 - 18 = 302 >= 18 mm in front of the wall -> no notch needed.
TEST(Hanger, HangersAndRailOnGroovedWallCabinet) {
    const GenerationResult r = gen(hungCabinet());
    const domain::Assembly& a = r.assembly;
    EXPECT_THAT(errorsOf(r.diagnostics), IsEmpty());
    const auto l = holes(a, "side.left", "hanger.left");
    ASSERT_EQ(l.size(), 2u);
    expectHole(l[0], domain::Face::B, 687, 312);
    expectHole(l[1], domain::Face::B, 655, 312);
    const auto rr = holes(a, "side.right", "hanger.right");
    ASSERT_EQ(rr.size(), 2u);
    expectHole(rr[0], domain::Face::A, 687, 312);
    expectHole(rr[1], domain::Face::A, 655, 312);

    const auto* rail = instance(a, "wall_rail");
    ASSERT_NE(rail, nullptr);
    EXPECT_EQ(rail->definition.str(), hardware::ids::kWallRailGeneric);
    EXPECT_EQ(rail->quantity, 1);
    EXPECT_TRUE(geometry::approxEqual(rail->position, {0, 320, 682}));
    EXPECT_THAT(rail->note, HasSubstr("Wall rail 600 mm (cut on site)"));
    ASSERT_EQ(rail->parts.size(), 2u);
    EXPECT_EQ(rail->parts[0].str(), "cab/side.left");

    // Back panel rear face at y = 302 (back_rear_y = depth - groove_distance).
    EXPECT_NEAR(part(a, "back").bounds().max.y, 302, 1e-9);
}

TEST(Hanger, IncompatibleBacksAreRefused) {
    auto overlay = generate(hungCabinet(BackMount::Overlay), libs());
    ASSERT_FALSE(overlay.ok());
    EXPECT_EQ(overlay.error().code, core::ErrorCode::ConstraintViolation);
    EXPECT_THAT(overlay.error().message, HasSubstr("Overlay back"));
    EXPECT_THAT(overlay.error().message, HasSubstr("18 mm"));

    auto inset = generate(hungCabinet(BackMount::Inset), libs());
    ASSERT_FALSE(inset.ok());
    EXPECT_THAT(inset.error().message, HasSubstr("Inset back"));

    // groove_distance 10 < back_space_min 18.
    auto shallow = generate(hungCabinet(BackMount::Grooved, 10), libs());
    ASSERT_FALSE(shallow.ok());
    EXPECT_THAT(shallow.error().message, HasSubstr("groove_distance is 10 mm"));

    // No back: nothing closes the space, hangers are placed.
    const GenerationResult none = gen(hungCabinet(BackMount::None));
    EXPECT_NE(instance(none.assembly, "hanger.left"), nullptr);
}

TEST(Hanger, MissingDefinitionAndUnsupportedCabinets) {
    CabinetDefinition d = hungCabinet();
    d.hardware.hanger = domain::HardwareId("hw.no_such_hanger");
    auto r = generate(d, libs());
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, core::ErrorCode::NotFound);
    EXPECT_THAT(r.error().message, HasSubstr("hw.no_such_hanger"));

    CabinetDefinition gap = templates::dishwasherFront(domain::AssemblyId("gap"), templateMaterials());
    gap.hardware.hanger = domain::HardwareId(hardware::ids::kHangerGeneric);
    auto g = generate(gap, libs());
    ASSERT_FALSE(g.ok());
    EXPECT_EQ(g.error().code, core::ErrorCode::Unsupported);
}

} // namespace fos::furniture::test
