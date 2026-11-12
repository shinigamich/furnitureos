// SPDX-License-Identifier: AGPL-3.0-only
// Ready-made templates: all generate cleanly; wardrobe hinge regression at default settings.
#include "test_support.hpp"

namespace fos::furniture::test {
using domain::Face;
using domain::Hole;
using ::testing::ElementsAre;
using ::testing::IsEmpty;

namespace {
std::vector<CabinetDefinition> allTemplates() {
    const auto m = templateMaterials();
    return {templates::baseCabinet(domain::AssemblyId("base"), m),   templates::wallCabinet(domain::AssemblyId("wall"), m),
            templates::tallCabinet(domain::AssemblyId("tall"), m),   templates::drawerUnit(domain::AssemblyId("drawers"), m),
            templates::wardrobeSection(domain::AssemblyId("wardrobe"), m),
            templates::openShelving(domain::AssemblyId("open"), m)};
}
std::vector<double> us(const std::vector<Hole>& hs) {
    std::vector<double> out;
    for (const auto& h : hs) out.push_back(h.u);
    return out;
}
} // namespace

TEST(Templates, EveryTemplateGeneratesWithoutErrorsAndDeterministically) {
    for (const CabinetDefinition& d : allTemplates()) {
        SCOPED_TRACE(d.name);
        auto r = generate(d, libs());
        ASSERT_TRUE(r.ok()) << r.error().message;
        EXPECT_THAT(errorsOf(r.value().diagnostics), IsEmpty());
        EXPECT_FALSE(hasCode(r.value().diagnostics, "furniture.hinge.near_shelf"));
        EXPECT_THAT(interpenetrations(r.value().assembly), IsEmpty());
        for (const auto& p : r.value().assembly.parts)
            EXPECT_THAT(errorsOf(machining::validateOperations(p)), IsEmpty()) << p.id.str();
        EXPECT_FALSE(r.value().assembly.parts.empty());
        EXPECT_EQ(dump(gen(d).assembly), dump(r.value().assembly));
    }
}

TEST(Templates, TemplateDimensions) {
    const auto m = templateMaterials();
    // Base: 600 x 820 x 560, sides to the floor, rails, pair of doors.
    const GenerationResult base = gen(templates::baseCabinet(domain::AssemblyId("base"), m));
    expectBox(part(base.assembly, "side.left"), {0, 0, 0}, {18, 560, 820});
    EXPECT_TRUE(has(base.assembly, "rail.front") && has(base.assembly, "rail.back") && has(base.assembly, "plinth"));
    EXPECT_TRUE(has(base.assembly, "door.left") && has(base.assembly, "door.right"));
    // Drawer unit: feet 100 -> carcass z 100..820, four fronts in a stack z 102..818
    // (716 - 3 * 3 = 707 shared 2:2:1:1 -> the lowest is 707 / 3 = 235.667 high).
    const GenerationResult drawers = gen(templates::drawerUnit(domain::AssemblyId("drawers"), m));
    expectBox(part(drawers.assembly, "bottom"), {18, 0, 100}, {582, 560, 118});
    EXPECT_NEAR(part(drawers.assembly, "drawer.1").width, 707.0 / 3.0, 1e-9);
    EXPECT_NEAR(part(drawers.assembly, "drawer.4").bounds().max.z, 818, 1e-9);
    EXPECT_NE(instance(drawers.assembly, "hw.feet"), nullptr);
}

TEST(Templates, WardrobeRightDoorIntermediateHingeIsMovedOffTheShelf) {
    // Reviewer scenario at DEFAULT shelf clearance (1 mm).
    const GenerationResult r = gen(templates::wardrobeSection(domain::AssemblyId("wardrobe"), templateMaterials()));
    const domain::Assembly& a = r.assembly;
    // 1000 wide: available 964 - 18 = 946 -> 473 per column; right column x 509..982.
    // Plinth 80: interior z 98..2182 (2084); one shelf: z 98 + (2084 - 18) / 2 = 1131 .. 1149, x 510..981
    // (1 mm from the right side at x 982), y 20 .. 580 (depth 580, setback 20).
    expectBox(part(a, "shelf.2.1"), {510, 20, 1131}, {981, 580, 1149});
    // Doors z 98 - 16 = 82 .. 2182 + 16 = 2198 (2116 high) -> 5 hinges; rule 100, 579, 1058, 1537, 2016.
    // Right door: the shelf occupies 1049..1067 above the door bottom; clearance 40 -> forbidden
    // 1009..1107; 1058 is 49 from both ends -> 1009 (cup at z 82 + 1009 = 1091).
    EXPECT_THAT(us(holes(a, "door.right", "hw.hinges.door.right")), ElementsAre(100, 579, 1009, 1537, 2016));
    // Plate holes on the right side (u = z): hinge z -/+ 16 -> the third hinge at 1075 / 1107, below the
    // shelf (1131) and its pin holes (z 1128.5, r 2.5, from 1126 up) - clear of both.
    const auto plates = holes(a, "side.right", "hw.hinges.door.right");
    EXPECT_THAT(us(plates), ElementsAre(166, 198, 645, 677, 1075, 1107, 1603, 1635, 2082, 2114));
    EXPECT_EQ(plates[4], (Hole{Face::A, 1075, 37, 5, 13, false}));
    for (const auto& h : plates) EXPECT_FALSE(h.u + 2.5 > 1131 - 2.5 && h.u - 2.5 < 1149) << h.u;
    // Left door: shelves at 500.4, 920.8, 1341.2, 1761.6 (space (2084 - 72) / 5 = 402.4) leave the
    // rule positions free (forbidden zones from the door bottom 378.4..476.4, 798.8..896.8, ...).
    EXPECT_THAT(us(holes(a, "door.left", "hw.hinges.door.left")), ElementsAre(100, 579, 1058, 1537, 2016));
    EXPECT_THAT(errorsOf(r.diagnostics), IsEmpty());
    EXPECT_FALSE(hasCode(r.diagnostics, "furniture.hinge.near_shelf"));
}

TEST(Templates, TemplatesAreEditable) {
    const Libraries l = libs();
    CabinetDefinition d = templates::wallCabinet(domain::AssemblyId("wall"), templateMaterials());
    auto r = setParameter(d, l, "width", 400);
    ASSERT_TRUE(r.ok()) << r.error().message;
    // 400 - 36 = 364 inner width; 2 shelves 364 - 2 * 1 = 362 long.
    expectSize(part(r.value().generated.assembly, "shelf.1"), 362, 320 - 20, 18);
    EXPECT_THAT(errorsOf(r.value().generated.diagnostics), IsEmpty());
}

} // namespace fos::furniture::test
