// SPDX-License-Identifier: AGPL-3.0-only
// Corner cabinets: blind corner (CornerKind::Blind) and L-shaped corner base cabinet (LShape).
#include "fos/geometry/contour.hpp"
#include "test_support.hpp"

namespace fos::furniture::test {
using ::testing::HasSubstr;
using ::testing::IsEmpty;

namespace {
CabinetDefinition blind() { return templates::blindCornerCabinet(domain::AssemblyId("bc"), templateMaterials()); }
CabinetDefinition lCorner() { return templates::lCornerCabinet(domain::AssemblyId("lc"), templateMaterials()); }
void setValue(CabinetDefinition& d, const char* name, double v) { ASSERT_TRUE(d.parameters.setValue(name, v).ok()); }
std::string errorOf(const CabinetDefinition& d) {
    auto r = generate(d, libs());
    return r.ok() ? std::string("(generated)") : r.error().message;
}
} // namespace

// 1000 x 820 x 560, plinth 100, 18 mm: front x 18 - 16 = 2 .. 982 + 16 = 998, z 118 - 16 = 102 ..
// 802 + 16 = 818, inner face y = -2. Split at blind_width 500, gap 3: panel 2..498.5, door 501.5..998.
TEST(CornerBlind, TemplateSplitsTheFront) {
    const GenerationResult r = gen(blind());
    const domain::Assembly& a = r.assembly;
    EXPECT_THAT(errorsOf(r.diagnostics), IsEmpty());
    EXPECT_THAT(interpenetrations(a), IsEmpty());
    expectBox(part(a, "blind"), {2, -20, 102}, {498.5, -2, 818});
    expectBox(part(a, "door"), {501.5, -20, 102}, {998, -2, 818});
    EXPECT_EQ(part(a, "blind").material.str(), "mat.pb18"); // front material = carcass (template front empty)
    ASSERT_NE(instance(a, "hw.hinges.door"), nullptr);
    EXPECT_FALSE(holes(a, "side.right", "hw.hinges.door").empty()); // hinged away from the blind side
    EXPECT_TRUE(holes(a, "side.left", "hw.hinges.door").empty());
    EXPECT_EQ(instance(a, "hw.hinges.blind"), nullptr); // the blind panel gets no fittings
}

// Blind on the right: split at 1000 - 500 = 500: door 2..498.5 (hinged left), panel 501.5..998.
TEST(CornerBlind, RightBlindSide) {
    CabinetDefinition d = blind();
    d.corner.blindSide = CornerSide::Right;
    d.interior.front->hingeSide = HingeSide::Left;
    const GenerationResult r = gen(d);
    EXPECT_THAT(errorsOf(r.diagnostics), IsEmpty());
    expectBox(part(r.assembly, "door"), {2, -20, 102}, {498.5, -2, 818});
    expectBox(part(r.assembly, "blind"), {501.5, -20, 102}, {998, -2, 818});
    EXPECT_FALSE(holes(r.assembly, "side.left", "hw.hinges.door").empty());
}

TEST(CornerBlind, Validation) {
    CabinetDefinition narrow = blind();
    setValue(narrow, corner_param::kBlindWidth, 40); // panel 40 - 1.5 - 2 = 36.5 < 50
    EXPECT_THAT(errorOf(narrow), HasSubstr("blind panel would be 36.5 mm wide"));
    CabinetDefinition wide = blind();
    setValue(wide, corner_param::kBlindWidth, 300); // door 998 - 301.5 = 696.5 > 600
    EXPECT_THAT(errorOf(wide), HasSubstr("door would be 696.5 mm wide"));
    CabinetDefinition small = blind();
    setValue(small, corner_param::kBlindWidth, 900); // door 998 - 901.5 = 96.5 < 150
    EXPECT_THAT(errorOf(small), HasSubstr("door would be 96.5 mm wide"));
    CabinetDefinition hinge = blind();
    hinge.interior.front->hingeSide = HingeSide::Left;
    EXPECT_THAT(errorOf(hinge), HasSubstr("hinge the door on the Right"));
    CabinetDefinition pair = blind();
    pair.interior.front->kind = FrontKind::DoorPair;
    EXPECT_THAT(errorOf(pair), HasSubstr("single door"));
    CabinetDefinition missing = blind();
    ASSERT_TRUE(missing.parameters.remove(corner_param::kBlindWidth).ok());
    EXPECT_THAT(errorOf(missing), HasSubstr("'blind_width'"));
}

// Legs 900 / 900, depths 560 / 560, 18 mm, plinth 100: yF = 560 - 900 = -340.
TEST(CornerL, TemplateGeometry) {
    const GenerationResult r = gen(lCorner());
    const domain::Assembly& a = r.assembly;
    EXPECT_THAT(errorsOf(r.diagnostics), IsEmpty());
    // The bounding boxes of door B and the L panels overlap; the rectilinear check must not report it.
    EXPECT_THAT(interpenetrations(a), IsEmpty());
    expectBox(part(a, "side.a"), {882, 0, 0}, {900, 560, 820});
    expectBox(part(a, "side.b"), {0, -340, 0}, {560, -322, 820});
    // Bottom 882 x 882 (x 0..882, y -322..560), notch x > 560, y < 0 (part y 322).
    const domain::Part bottom = part(a, "bottom");
    expectBox(bottom, {0, -322, 100}, {882, 560, 118});
    ASSERT_TRUE(bottom.shape.has_value());
    EXPECT_EQ(bottom.shape->outer.size(), 6u);
    EXPECT_NEAR(bottom.shape->outer.vertices[2].point.x, 560, 1e-9);
    EXPECT_NEAR(bottom.shape->outer.vertices[2].point.y, 322, 1e-9);
    // Area 882 * 882 - 322 * 322 = 674240.
    EXPECT_NEAR(geometry::signedArea(bottom.shape->outer), 674240, 1e-6);
    ASSERT_EQ(bottom.shape->outerEdges.size(), 6u); // the two front segments are banded
    EXPECT_EQ(bottom.shape->outerEdges[1], domain::EdgeBandId("eb.abs2"));
    EXPECT_EQ(bottom.shape->outerEdges[2], domain::EdgeBandId("eb.abs2"));
    EXPECT_FALSE(bottom.shape->outerEdges[0].has_value());
    expectBox(part(a, "top"), {0, -322, 802}, {882, 560, 820});
    // Shelf: clear height 802 - 118 = 684, space (684 - 18) / 2 = 333 -> z 451. Clearance 1,
    // setback 20: x 0..881, y -321..560, notch at x 560 - 20 = 540, part y 20 + 321 = 341.
    const domain::Part shelf = part(a, "shelf.1");
    expectBox(shelf, {0, -321, 451}, {881, 560, 469});
    ASSERT_TRUE(shelf.shape.has_value());
    EXPECT_NEAR(shelf.shape->outer.vertices[2].point.x, 540, 1e-9);
    EXPECT_NEAR(shelf.shape->outer.vertices[2].point.y, 341, 1e-9);
    // Plinths (setback 50): A x 560 - 50 - 18 = 492 .. 882, y 50..68; B x 492..510, y -322..50.
    expectBox(part(a, "plinth.a"), {492, 50, 0}, {882, 68, 100});
    expectBox(part(a, "plinth.b"), {492, -322, 0}, {510, 50, 100});
    // Backs (3 mm, inset 2, z 100 + 2 .. 820 - 2): A x -3..898 behind y 560; B x -3..0, y -338..560.
    expectBox(part(a, "back.a"), {-3, 560, 102}, {898, 563, 818});
    expectBox(part(a, "back.b"), {-3, -338, 102}, {0, 560, 818});
    // Doors z 102..818. A: x 560 + 2 + 18 + 3 = 583 .. 898 (315). B: x 562..580, y -340 + 2 = -338 .. -20 (318).
    expectBox(part(a, "door.a"), {583, -20, 102}, {898, -2, 818});
    expectBox(part(a, "door.b"), {562, -338, 102}, {580, -20, 818});
    EXPECT_FALSE(holes(a, "side.a", "hw.hinges.door.a").empty());
    EXPECT_FALSE(holes(a, "side.b", "hw.hinges.door.b").empty());
    EXPECT_NE(instance(a, "hw.handle.door.a"), nullptr);
    EXPECT_NE(instance(a, "hw.joint.bottom.side.b"), nullptr);
    EXPECT_NE(instance(a, "hw.joint.top.side.a"), nullptr);
    // Shelf pins: 2 per end side at u = 451 - 5 / 2 = 448.5; side A v = 20 + 50 and 560 - 50,
    // side B v = 560 - 20 - 50 and 50.
    const auto pinsA = holes(a, "side.a", "hw.pins.shelf.1"), pinsB = holes(a, "side.b", "hw.pins.shelf.1");
    ASSERT_EQ(pinsA.size(), 2u);
    ASSERT_EQ(pinsB.size(), 2u);
    EXPECT_NEAR(pinsA[0].u, 448.5, 1e-9);
    EXPECT_NEAR(pinsA[0].v, 70, 1e-9);
    EXPECT_NEAR(pinsA[1].v, 510, 1e-9);
    EXPECT_NEAR(pinsB[0].v, 490, 1e-9);
    EXPECT_NEAR(pinsB[1].v, 50, 1e-9);
    EXPECT_EQ(instance(a, "hw.pins.shelf.1")->quantity, 4);
}

TEST(CornerL, Validation) {
    CabinetDefinition narrow = lCorner();
    setValue(narrow, param::kWidth, 700); // door A 698 - 583 = 115 < 150
    EXPECT_THAT(errorOf(narrow), HasSubstr("door A (leg A) would be 115 mm wide"));
    CabinetDefinition legB = lCorner();
    setValue(legB, corner_param::kWidthB, 1300); // door B -20 - (560 - 1300 + 2) = 718 > 600
    EXPECT_THAT(errorOf(legB), HasSubstr("door B (leg B) would be 718 mm wide"));
    CabinetDefinition rails = lCorner();
    rails.construction.top = TopKind::Rails;
    EXPECT_THAT(errorOf(rails), HasSubstr("stretcher rails are not supported"));
    CabinetDefinition split = lCorner();
    split.interior.split = SplitDirection::Vertical;
    split.interior.children = {Section{}, Section{}};
    EXPECT_THAT(errorOf(split), HasSubstr("cannot take dividers"));
    CabinetDefinition single = lCorner();
    single.interior.front->kind = FrontKind::Door;
    EXPECT_THAT(errorOf(single), HasSubstr("Full-overlay DoorPair"));
    // Open corner unit (no front) and no plinth also generate cleanly.
    CabinetDefinition open = lCorner();
    open.interior.front.reset();
    open.construction.plinth = PlinthKind::None;
    const GenerationResult r = gen(open);
    EXPECT_THAT(errorsOf(r.diagnostics), IsEmpty());
    EXPECT_THAT(interpenetrations(r.assembly), IsEmpty());
    EXPECT_FALSE(has(r.assembly, "door.a"));
    EXPECT_FALSE(has(r.assembly, "plinth.a"));
}

// A part overlapping the NOTCH of an L-shaped part's bounding box does not interpenetrate it;
// one overlapping the solid leg does (overlap reported on the leg only).
TEST(CornerL, RectilinearInterpenetrationCheck) {
    domain::Assembly a;
    domain::Part l;
    l.id = domain::PartId("a/l");
    l.length = 100;
    l.width = 100;
    l.thickness = 10;
    l.shape = domain::PartShape{};
    for (const auto& [x, y] : {std::pair{0.0, 0.0}, {40.0, 0.0}, {40.0, 60.0}, {100.0, 60.0}, {100.0, 100.0}, {0.0, 100.0}})
        l.shape->outer.vertices.push_back({geometry::Vec2{x, y}, 0.0});
    domain::Part inNotch = l;
    inNotch.id = domain::PartId("a/notch");
    inNotch.shape.reset();
    inNotch.length = 50;
    inNotch.width = 50;
    inNotch.placement = geometry::Transform::translation({45, 5, 0});
    a.parts = {l, inNotch};
    EXPECT_TRUE(findInterpenetrations(a).empty());
    a.parts[1].placement = geometry::Transform::translation({30, 5, 0}); // x 30..80 meets the leg x 30..40
    const auto found = findInterpenetrations(a);
    ASSERT_EQ(found.size(), 1u);
    EXPECT_NEAR(found[0].overlap.min.x, 30, 1e-9);
    EXPECT_NEAR(found[0].overlap.max.x, 40, 1e-9);
    EXPECT_NEAR(found[0].overlap.max.y, 55, 1e-9);
}

} // namespace fos::furniture::test
