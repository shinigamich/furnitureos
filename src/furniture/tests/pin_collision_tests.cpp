// SPDX-License-Identifier: AGPL-3.0-only
// Shelf-pin collision rule (gen_hardware.cpp, README "Shelf pins and joints"): joints win,
// a shelf whose pins would cut into an existing hole loses its pins with a warning.
#include "test_support.hpp"

namespace fos::furniture::test {
using ::testing::ElementsAre;
using ::testing::HasSubstr;
using ::testing::IsEmpty;

namespace {
std::vector<std::string> collisions(const GenerationResult& r) {
    std::vector<std::string> out;
    for (const auto& d : r.diagnostics)
        if (d.code.find("collision") != std::string::npos) out.push_back(d.code + ": " + d.message);
    return out;
}
GenerationResult wardrobeAt(double height) {
    CabinetDefinition d = templates::wardrobeSection(domain::AssemblyId("wardrobe"), templateMaterials());
    auto r = setParameter(d, libs(), "height", height);
    EXPECT_TRUE(r.ok()) << (r.ok() ? "" : r.error().message);
    return r.ok() ? r.value().generated : GenerationResult{};
}
} // namespace

TEST(PinCollision, WardrobeAt337And320DropsOnlyTheLowestLeftShelfPins) {
    // Height 337, plinth 80: interior z 98..319 = 221 mm. Left column, 4 shelves: space
    // (221 - 4 * 18) / 5 = 29.8 -> shelf.1.1 at z 127.8. Its pin holes (diameter 5) are
    // centred 2.5 mm below the shelf, at z 125.3 = u 27.3 on the partition (u from its lower
    // end at z 98), i.e. u 24.8..29.8, 8 mm deep (shared-panel rule). The partition's lower end
    // carries the confirmat edge hole (diameter 5, 37 deep along u, centred in the 18 mm board:
    // 6.5..11.5 through the thickness) -> they overlap (u 24.8 < 37, thickness 6.5 < 8).
    // shelf.1.4 at 98 + 4 * 29.8 + 3 * 18 = 271.2 -> u 170.7 (168.2..173.2), clear of the upper
    // confirmat (u 221 - 37 = 184..221). Right column: one shelf at 98 + (221 - 18) / 2 = 199.5.
    // Height 320: interior 204, space 26.4 -> shelf.1.1 at 124.4 -> u 23.9 (collides),
    // shelf.1.4 at 257.6 -> u 157.1 (clear of 204 - 37 = 167).
    for (const double h : {337.0, 320.0}) {
        SCOPED_TRACE(h);
        const GenerationResult r = wardrobeAt(h);
        EXPECT_THAT(collisions(r), IsEmpty());
        EXPECT_THAT(errorsOf(r.diagnostics), IsEmpty());
        const auto dropped = messagesWithCode(r.diagnostics, "furniture.shelf_pins.dropped");
        ASSERT_EQ(dropped.size(), 1u);
        EXPECT_THAT(dropped[0], HasSubstr("'Shelf' (wardrobe/shelf.1.1) were not drilled"));
        EXPECT_THAT(dropped[0], HasSubstr("wardrobe/hw.joint.partition.1.bottom/"));
        EXPECT_EQ(instance(r.assembly, "hw.pins.shelf.1.1"), nullptr);
        EXPECT_NE(instance(r.assembly, "hw.pins.shelf.1.2"), nullptr);
        EXPECT_NE(instance(r.assembly, "hw.pins.shelf.1.4"), nullptr);
        EXPECT_NE(instance(r.assembly, "hw.pins.shelf.2.1"), nullptr);
        EXPECT_THAT(holes(r.assembly, "side.left", "hw.pins.shelf.1.1"), IsEmpty());
        for (const auto& p : r.assembly.parts) EXPECT_THAT(errorsOf(machining::validateOperations(p)), IsEmpty()) << p.id.str();
    }
    // At the default height (2200) nothing is dropped.
    const GenerationResult normal = wardrobeAt(2200);
    EXPECT_FALSE(hasCode(normal.diagnostics, "furniture.shelf_pins.dropped"));
    EXPECT_NE(instance(normal.assembly, "hw.pins.shelf.1.1"), nullptr);
}

TEST(PinCollision, TemplateHeightSweepHasNoMachiningCollisions) {
    // Every template at heights 200..3000 mm in 7 mm steps plus the reported 320 / 337: every
    // height that generates must be free of machining collisions (heights a template refuses,
    // e.g. too low for its drawers, are skipped).
    const auto m = templateMaterials();
    const std::vector<CabinetDefinition> all = {
        templates::baseCabinet(domain::AssemblyId("base"), m),   templates::wallCabinet(domain::AssemblyId("wall"), m),
        templates::tallCabinet(domain::AssemblyId("tall"), m),   templates::drawerUnit(domain::AssemblyId("drawers"), m),
        templates::wardrobeSection(domain::AssemblyId("wardrobe"), m), templates::openShelving(domain::AssemblyId("open"), m)};
    std::vector<double> heights = {320, 337};
    for (int h = 200; h <= 3000; h += 7) heights.push_back(h);
    const Libraries l = libs();
    std::size_t generated = 0;
    for (const auto& t : all)
        for (const double h : heights) {
            CabinetDefinition d = t;
            auto r = setParameter(d, l, "height", h);
            if (!r) continue;
            ++generated;
            EXPECT_THAT(collisions(r.value().generated), IsEmpty()) << t.name << " at " << h;
        }
    EXPECT_GT(generated, 2000u); // 6 templates x 403 heights = 2418, the tall cabinet refuses the lowest
}

} // namespace fos::furniture::test
