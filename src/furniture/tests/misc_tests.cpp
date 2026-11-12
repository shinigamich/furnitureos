// SPDX-License-Identifier: AGPL-3.0-only
// Groove end rule, interpenetration self-check, enum texts, determinism.
#include "test_support.hpp"

namespace fos::furniture::test {
using domain::Face;
using domain::Groove;
using ::testing::ElementsAre;
using ::testing::IsEmpty;

namespace {
std::vector<Groove> grooves(const domain::Assembly& a, const char* slot) {
    std::vector<Groove> out;
    for (const auto& op : part(a, slot).operations)
        if (const auto* g = std::get_if<Groove>(&op.shape)) out.push_back(*g);
    return out;
}
domain::Part box(const char* id, const char* name, geometry::Vec3 at) {
    domain::Part p;
    p.id = domain::PartId(id);
    p.name = name;
    p.length = 100;
    p.width = 100;
    p.thickness = 10;
    p.placement = geometry::Transform::translation(at);
    return p;
}
} // namespace

// Grooved back of the default cabinet: groove depth 8, clearance 1 -> engagement 7; centre
// y = 560 - 10 - 1.5 = 548.5, 3 wide. Back x = 18 - 7 = 11, width 564 + 14 = 578 (x 11..589);
// z = interior_bottom - 7, height 684 + 14 = 698.
TEST(Misc, GroovesStopAtExposedEndsAndRunThroughHiddenOnes) {
    ConstructionOptions o;
    o.back = BackMount::Grooved;
    o.topJoint = o.bottomJoint = JointKind::OverSides;
    GenerationResult r = gen(defaultCabinet(o));
    // Both OverSides: sides z 18..702 (684 long), both ends covered -> through 0..684.
    EXPECT_THAT(grooves(r.assembly, "side.left"), ElementsAre(Groove{Face::B, 0, 548.5, 684, 548.5, 3, 8}));
    // Bottom / top run 0..600 with banded exposed ends -> stop 1 mm beyond the back: 10..590,
    // in line with the bottoms of the side grooves (x 18 - 8 = 10 and 582 + 8 = 590).
    EXPECT_THAT(grooves(r.assembly, "bottom"), ElementsAre(Groove{Face::A, 10, 548.5, 590, 548.5, 3, 8}));
    EXPECT_THAT(grooves(r.assembly, "top"), ElementsAre(Groove{Face::B, 10, 548.5, 590, 548.5, 3, 8}));
    EXPECT_THAT(interpenetrations(r.assembly), IsEmpty()); // the back lies inside every (stopped) groove
    EXPECT_THAT(errorsOf(r.diagnostics), IsEmpty());

    // Top over, bottom between: sides z 0..702; lower end exposed -> from back_z - 1 = 11 - 1 = 10;
    // upper end under the top -> through to 702. Bottom ends at the sides -> 0..564.
    o.bottomJoint = JointKind::BetweenSides;
    r = gen(defaultCabinet(o));
    EXPECT_THAT(grooves(r.assembly, "side.right"), ElementsAre(Groove{Face::A, 10, 548.5, 702, 548.5, 3, 8}));
    EXPECT_THAT(grooves(r.assembly, "bottom"), ElementsAre(Groove{Face::A, 0, 548.5, 564, 548.5, 3, 8}));
    EXPECT_THAT(grooves(r.assembly, "top"), ElementsAre(Groove{Face::B, 10, 548.5, 590, 548.5, 3, 8}));
    EXPECT_THAT(interpenetrations(r.assembly), IsEmpty());

    // Rails (between the sides) and sides to the floor: plinth 100 -> interior z 118..702 (584), back
    // z 118 - 7 = 111, height 584 + 14 = 598 -> z 111..709. Sides z 0..720, both ends banded ->
    // 111 - 1 = 110 .. 709 + 1 = 710. Rear rail between the sides -> through 0..564 at its own y: 548.5 - (560 - 100) = 88.5.
    o = {};
    o.back = BackMount::Grooved;
    o.top = TopKind::Rails;
    o.plinth = PlinthKind::SidesToFloor;
    r = gen(defaultCabinet(o));
    EXPECT_THAT(grooves(r.assembly, "side.left"), ElementsAre(Groove{Face::B, 110, 548.5, 710, 548.5, 3, 8}));
    EXPECT_THAT(grooves(r.assembly, "rail.back"), ElementsAre(Groove{Face::B, 0, 88.5, 564, 88.5, 3, 8}));
    EXPECT_THAT(grooves(r.assembly, "bottom"), ElementsAre(Groove{Face::A, 0, 548.5, 564, 548.5, 3, 8}));
    EXPECT_THAT(interpenetrations(r.assembly), IsEmpty());
    EXPECT_THAT(errorsOf(r.diagnostics), IsEmpty());
}

TEST(Misc, InterpenetrationCheckReportsSharedVolumeOutsideGrooves) {
    domain::Assembly a;
    a.id = domain::AssemblyId("a");
    a.parts = {box("a/p1", "P1", {0, 0, 0}), box("a/p2", "P2", {95, 0, 0})};
    // Shared x 95..100, y 0..100, z 0..10.
    auto found = findInterpenetrations(a);
    ASSERT_EQ(found.size(), 1u);
    EXPECT_EQ(found[0].first, 0u);
    EXPECT_EQ(found[0].second, 1u);
    EXPECT_EQ(describe(a, found[0]),
              "'P1' (a/p1) and 'P2' (a/p2) interpenetrate by 5 x 100 x 10 mm (x 95..100, y 0..100, z 0..10)");
    // A groove in P1 along local y at u = 97.5, 5 wide, 10 deep from face A covers exactly the shared volume.
    domain::Operation op;
    op.id = domain::OperationId("a/p1/op.g");
    op.shape = Groove{Face::A, 97.5, 0, 97.5, 100, 5, 10};
    a.parts[0].operations.push_back(op);
    EXPECT_THAT(findInterpenetrations(a), IsEmpty());
    // 9 deep leaves z 0..1 of the shared volume outside the groove.
    std::get<Groove>(a.parts[0].operations[0].shape).depth = 9;
    EXPECT_EQ(findInterpenetrations(a).size(), 1u);
    // Touching boxes do not interpenetrate.
    a.parts[0].operations.clear();
    a.parts[1].placement = geometry::Transform::translation({100, 0, 0});
    EXPECT_THAT(findInterpenetrations(a), IsEmpty());
}

TEST(Misc, EnumTextsRoundTrip) {
    for (const auto k : {BackMount::None, BackMount::Overlay, BackMount::Grooved, BackMount::Inset})
        EXPECT_EQ(fromString<BackMount>(toString(k)), k);
    for (const auto k : {FrontKind::Door, FrontKind::DoorPair, FrontKind::DrawerStack})
        EXPECT_EQ(fromString<FrontKind>(toString(k)), k);
    for (const auto k : {SizeKind::Fixed, SizeKind::Weight, SizeKind::Expression})
        EXPECT_EQ(fromString<SizeKind>(toString(k)), k);
    EXPECT_FALSE(fromString<JointKind>("betweensides").has_value()); // case-sensitive
}

TEST(Misc, GenerationIsDeterministicAndPure) {
    const CabinetDefinition d = templates::wardrobeSection(domain::AssemblyId("w"), templateMaterials());
    const CabinetDefinition copy = d;
    const GenerationResult r1 = gen(d), r2 = gen(copy);
    EXPECT_EQ(dump(r1.assembly), dump(r2.assembly));
    ASSERT_EQ(r1.diagnostics.size(), r2.diagnostics.size());
    for (std::size_t i = 0; i < r1.diagnostics.size(); ++i) {
        EXPECT_EQ(r1.diagnostics[i].code, r2.diagnostics[i].code);
        EXPECT_EQ(r1.diagnostics[i].message, r2.diagnostics[i].message);
    }
    EXPECT_EQ(r1.values, r2.values);
    // generate() does not modify its input (it resolves a private copy).
    EXPECT_TRUE(d.interior == copy.interior);
    EXPECT_EQ(d.pinnedParameters, copy.pinnedParameters);
}

} // namespace fos::furniture::test
