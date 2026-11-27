// SPDX-License-Identifier: AGPL-3.0-only
// Panel desks and tables (DeskSpec, README "Desks and tables").
#include "fos/geometry/contour.hpp"
#include "test_support.hpp"

#include <algorithm>

namespace fos::furniture::test {
using ::testing::HasSubstr;

namespace {
Libraries deskLibs() {
    Libraries l = libs();
    l.hardware.push_back(genericLegDefinition());
    return l;
}
CabinetDefinition desk() { return templates::desk(domain::AssemblyId("dk"), templateMaterials()); }
CabinetDefinition table() { return templates::table(domain::AssemblyId("tb"), templateMaterials()); }
GenerationResult gen(const CabinetDefinition& d) {
    auto r = generate(d, deskLibs());
    EXPECT_TRUE(r.ok()) << (r.ok() ? "" : r.error().message);
    return r.ok() ? std::move(r.value()) : GenerationResult{};
}
std::string errorOf(const CabinetDefinition& d) {
    auto r = generate(d, deskLibs());
    return r.ok() ? std::string("(generated)") : r.error().message;
}
const domain::Part* part(const domain::Assembly& a, const std::string& id) {
    for (const auto& p : a.parts)
        if (p.id.str() == id) return &p;
    ADD_FAILURE() << "no part " << id;
    return nullptr;
}
std::size_t countWithPrefix(const domain::Assembly& a, const std::string& prefix) {
    return static_cast<std::size_t>(std::count_if(a.parts.begin(), a.parts.end(),
                                                  [&](const domain::Part& p) { return p.id.str().rfind(prefix, 0) == 0; }));
}
bool hasDiag(const GenerationResult& r, const std::string& code) {
    for (const auto& d : r.diagnostics)
        if (d.code == code) return true;
    return false;
}
void expectNoErrors(const GenerationResult& r) {
    for (const auto& d : r.diagnostics) EXPECT_NE(d.severity, core::Severity::Error) << d.code << ": " << d.message;
}
} // namespace

// Desk 1400 x 740 x 700, 18 mm: top z 722..740; ends x 0..18 / 1382..1400, 722 high, 700 deep;
// modesty 1364 x 350, rear face at y 700 - 100 = 600 (y 582..600), z 372..722.
// Pedestal: x 1382 - 400 = 982 .. 1382, depth 700 - 100 - 18 = 582, height 722.
TEST(Desk, TemplateGeometry) {
    const GenerationResult r = gen(desk());
    expectNoErrors(r);
    EXPECT_FALSE(hasDiag(r, "furniture.desk.knee_clearance"));
    const auto& a = r.assembly;
    const domain::Part* top = part(a, "dk/top");
    ASSERT_NE(top, nullptr);
    EXPECT_DOUBLE_EQ(top->length, 1400);
    EXPECT_DOUBLE_EQ(top->width, 700);
    const auto tb = top->bounds();
    EXPECT_NEAR(tb.min.z, 722, 1e-9);
    EXPECT_NEAR(tb.max.z, 740, 1e-9);
    const domain::Part* right = part(a, "dk/end.right");
    ASSERT_NE(right, nullptr);
    const auto rb = right->bounds();
    EXPECT_NEAR(rb.min.x, 1382, 1e-9);
    EXPECT_NEAR(rb.max.z, 722, 1e-9);
    const domain::Part* modesty = part(a, "dk/modesty");
    ASSERT_NE(modesty, nullptr);
    const auto mb = modesty->bounds();
    EXPECT_NEAR(mb.min.x, 18, 1e-9);
    EXPECT_NEAR(mb.max.x, 1382, 1e-9);
    EXPECT_NEAR(mb.min.y, 582, 1e-9);
    EXPECT_NEAR(mb.max.y, 600, 1e-9);
    EXPECT_NEAR(mb.min.z, 372, 1e-9);
    // Pedestal side.left at x 982..1000.
    const domain::Part* ps = part(a, "dk/pedestal/side.left");
    ASSERT_NE(ps, nullptr);
    EXPECT_EQ(ps->custom.at("furniture.slot"), "pedestal/side.left");
    const auto pb = ps->bounds();
    EXPECT_NEAR(pb.min.x, 982, 1e-9);
    EXPECT_NEAR(pb.max.y, 582, 1e-9);
    EXPECT_EQ(countWithPrefix(a, "dk/pedestal/drawer."), 3u * 6u); // 3 fronts + 5 box parts each
    EXPECT_TRUE(findInterpenetrations(a).empty());
}

// Joints: confirmats from the top into both ends and from the ends into the modesty panel.
TEST(Desk, JointsUseConnectorPlacement) {
    const GenerationResult r = gen(desk());
    std::set<std::string> ids;
    for (const auto& h : r.assembly.hardware) ids.insert(h.id.str());
    for (const char* id : {"dk/hw.joint.end.left.top", "dk/hw.joint.end.right.top", "dk/hw.joint.modesty.end.left",
                           "dk/hw.joint.modesty.end.right"})
        EXPECT_TRUE(ids.count(id)) << id;
    CabinetDefinition d = desk();
    d.construction.dividerFastener = JointFastener::Dowel;
    const GenerationResult rd = gen(d);
    for (const auto& h : rd.assembly.hardware)
        if (h.id.str() == "dk/hw.joint.modesty.end.left") EXPECT_EQ(h.definition.str(), hardware::ids::kDowel8x30);
}

// Cable slot 300 x 60, 20 from the rear: x 550..850, y 620..680 in the top's frame, radius 30.
TEST(Desk, CableSlotAndRoundedCorners) {
    CabinetDefinition d = desk();
    d.desk->frontCornerRadius = 40;
    const GenerationResult r = gen(d);
    expectNoErrors(r);
    const domain::Part* top = part(r.assembly, "dk/top");
    ASSERT_NE(top, nullptr);
    ASSERT_TRUE(top->shape.has_value());
    ASSERT_EQ(top->shape->cutouts.size(), 1u);
    const auto cb = geometry::bounds(top->shape->cutouts[0]);
    EXPECT_NEAR(cb.min.x, 550, 1e-6);
    EXPECT_NEAR(cb.max.x, 850, 1e-6);
    EXPECT_NEAR(cb.min.y, 620, 1e-6);
    EXPECT_NEAR(cb.max.y, 680, 1e-6);
    // Rounded front corners: exactly two arc segments; the bounding box stays 1400 x 700.
    const auto arcs = std::count_if(top->shape->outer.vertices.begin(), top->shape->outer.vertices.end(),
                                    [](const geometry::ContourVertex& v) { return v.bulge != 0.0; });
    EXPECT_EQ(arcs, 2);
    const auto ob = geometry::bounds(top->shape->outer);
    EXPECT_NEAR(ob.max.x, 1400, 1e-6);
    EXPECT_NEAR(ob.max.y, 700, 1e-6);
}

// Table 1600 x 750 x 800 on legs: axes 70 from the edges, plate holes +-30 -> 16 holes, 4 legs.
TEST(Desk, TableLegsDrillPlateHoles) {
    const GenerationResult r = gen(table());
    expectNoErrors(r);
    EXPECT_EQ(r.assembly.parts.size(), 1u);
    const domain::Part* top = part(r.assembly, "tb/top");
    ASSERT_NE(top, nullptr);
    EXPECT_EQ(top->operations.size(), 16u);
    std::size_t legs = 0;
    for (const auto& h : r.assembly.hardware)
        if (h.definition.str() == kGenericLegId) ++legs;
    EXPECT_EQ(legs, 4u);
    // Front-left leg holes: x 40 / 100, y 40 / 100 on the underside.
    std::set<std::pair<long, long>> uv;
    for (const auto& op : top->operations) {
        const auto& h = std::get<domain::Hole>(op.shape);
        EXPECT_DOUBLE_EQ(h.depth, 10);
        const auto p = machining::faceToAssembly(*top, h.face, h.u, h.v);
        EXPECT_NEAR(p.z, 732, 1e-9);
        uv.insert({std::lround(p.x), std::lround(p.y)});
    }
    EXPECT_TRUE(uv.count({40, 40}));
    EXPECT_TRUE(uv.count({1560, 760}));
}

TEST(Desk, MissingLegDefinitionIsAnError) {
    auto r = generate(table(), libs());
    ASSERT_FALSE(r.ok());
    EXPECT_THAT(r.error().message, HasSubstr("hw.leg_generic"));
    EXPECT_EQ(requiredGenericHardware(table()).size(), 1u);
    EXPECT_TRUE(requiredGenericHardware(desk()).empty());
}

TEST(Desk, KneeClearanceWarning) {
    CabinetDefinition d = desk();
    ASSERT_TRUE(d.parameters.setValue("height", 600).ok()); // 582 clear < 600
    const GenerationResult r = gen(d);
    EXPECT_TRUE(hasDiag(r, "furniture.desk.knee_clearance"));
    CabinetDefinition w = desk();
    w.desk->pedestal->width = 800; // 1364 - 800 = 564 clear width
    EXPECT_TRUE(hasDiag(gen(w), "furniture.desk.knee_clearance"));
}

TEST(Desk, ValidationErrors) {
    CabinetDefinition d = desk();
    ASSERT_TRUE(d.parameters.setValue("width", 3200).ok());
    EXPECT_THAT(errorOf(d), HasSubstr("600 .. 3000"));
    d = desk();
    d.desk->modestyHeight = 800;
    EXPECT_THAT(errorOf(d), HasSubstr("modesty panel height"));
    d = table();
    d.desk->modestyPanel = true;
    EXPECT_THAT(errorOf(d), HasSubstr("modesty panel"));
    d = desk();
    d.desk->cableCutout->length = 1500;
    EXPECT_THAT(errorOf(d), HasSubstr("cable slot spans"));
    d = desk();
    d.desk->frontCornerRadius = 400;
    EXPECT_THAT(errorOf(d), HasSubstr("front corner radius"));
    d = table();
    d.desk->frontCornerRadius = 150; // plate edge 30 from the edges: (150 - 30) * 1.414 > 150
    EXPECT_THAT(errorOf(d), HasSubstr("cuts into the leg plates"));
    d = desk();
    d.desk->pedestal->drawers = 0;
    EXPECT_THAT(errorOf(d), HasSubstr("drawers"));
    d = desk();
    d.interior.shelfCount = 1;
    EXPECT_THAT(errorOf(d), HasSubstr("interior layout"));
}

TEST(Desk, LeftPedestalWithoutModestyIsFullDepth) {
    CabinetDefinition d = desk();
    d.desk->modestyPanel = false;
    d.desk->pedestal->side = DeskPedestalSide::Left;
    const GenerationResult r = gen(d);
    expectNoErrors(r);
    const domain::Part* ps = part(r.assembly, "dk/pedestal/side.left");
    ASSERT_NE(ps, nullptr);
    const auto pb = ps->bounds();
    EXPECT_NEAR(pb.min.x, 18, 1e-9);
    EXPECT_NEAR(pb.max.y, 700, 1e-9);
    EXPECT_TRUE(findInterpenetrations(r.assembly).empty());
}

} // namespace fos::furniture::test
