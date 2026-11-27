// SPDX-License-Identifier: AGPL-3.0-only
// Sliding-door wardrobes (FrontKind::SlidingDoors) with the generic track system.
#include "fos/machining/face.hpp"
#include "test_support.hpp"

namespace fos::furniture::test {
using ::testing::HasSubstr;
using ::testing::IsEmpty;

namespace {
/// Wardrobe 1800 x 2400 x 600 (default options: top / bottom between the sides, overlay back,
/// interior_depth = depth = 600): opening x 18..1782 (1764), z 18..2382 (2364).
CabinetDefinition wardrobe(int doors, double depth = 600) {
    CabinetDefinition d = defaultCabinet();
    parametric::Transaction tx;
    tx.setValue("width", 1800).setValue("height", 2400).setValue("depth", depth);
    EXPECT_TRUE(d.parameters.apply(tx).ok());
    FrontSpec f;
    f.kind = FrontKind::SlidingDoors;
    f.sliding.count = doors;
    d.interior.front = f;
    Section left, right;
    left.shelfCount = 4;
    right.shelfCount = 1;
    d.interior.split = SplitDirection::Vertical;
    d.interior.children = {left, right};
    return d;
}
std::size_t opsFrom(const domain::Part& p, const std::string& source) {
    std::size_t n = 0;
    for (const auto& op : p.operations) n += op.source == source;
    return n;
}
} // namespace

// 2 doors: width (1764 + 30) / 2 = 897, height 2364 - 40 = 2324, bottom at 18 + 15 = 33.
// Door 1 back lane (front face y = 10 + 10 + 35 = 55), door 2 front lane (y = 20), x 885..1782.
TEST(SlidingDoors, TwoDoorWardrobe) {
    const GenerationResult r = gen(wardrobe(2));
    const domain::Assembly& a = r.assembly;
    EXPECT_THAT(errorsOf(r.diagnostics), IsEmpty());
    EXPECT_THAT(interpenetrations(a), IsEmpty());
    expectSize(part(a, "sliding.1"), 2324, 897, 18);
    expectBox(part(a, "sliding.1"), {18, 55, 33}, {915, 73, 2357});
    expectBox(part(a, "sliding.2"), {885, 20, 33}, {1782, 38, 2357});
    EXPECT_EQ(part(a, "sliding.1").custom.at("furniture.sliding_plane"), "back");
    EXPECT_EQ(part(a, "sliding.2").custom.at("furniture.sliding_plane"), "front");
    EXPECT_FALSE(has(a, "sliding.3"));
    // Tracks: 1764 mm, ceil((1764 - 2 * 50) / 300) + 1 = 7 screw holes each.
    ASSERT_NE(instance(a, "hw.track.top"), nullptr);
    EXPECT_EQ(instance(a, "hw.track.top")->definition.str(), hardware::ids::kSlidingGeneric);
    EXPECT_THAT(instance(a, "hw.track.top")->note, HasSubstr("1764"));
    EXPECT_DOUBLE_EQ(instance(a, "hw.track.top")->length, 1764.0); // linear fitting length, mm
    ASSERT_NE(instance(a, "hw.track.bottom"), nullptr);
    EXPECT_DOUBLE_EQ(instance(a, "hw.track.bottom")->length, 1764.0);
    EXPECT_EQ(holes(a, "top", "hw.track.top").size(), 7u);
    EXPECT_EQ(holes(a, "bottom", "hw.track.bottom").size(), 7u);
    const domain::Part top = part(a, "top");
    const domain::Hole first = holes(a, "top", "hw.track.top").front();
    const auto pt = machining::faceToAssembly(top, first.face, first.u, first.v);
    EXPECT_NEAR(pt.x, 68, 1e-9);   // 18 + 50
    EXPECT_NEAR(pt.y, 50, 1e-9);   // track centre 10 + 80 / 2
    EXPECT_NEAR(pt.z, 2382, 1e-9); // underside of the top
    // Rollers: 4 holes on the rear face of each door; door 1 at x 58 / 875, z 63 / 2327, y 73.
    const auto rollers = holes(a, "sliding.1", "hw.rollers.sliding.1");
    ASSERT_EQ(rollers.size(), 4u);
    const auto r0 = machining::faceToAssembly(part(a, "sliding.1"), rollers[0].face, rollers[0].u, rollers[0].v);
    EXPECT_TRUE(geometry::approxEqual(r0, {58, 73, 63}, 1e-9));
    const auto r3 = machining::faceToAssembly(part(a, "sliding.1"), rollers[3].face, rollers[3].u, rollers[3].v);
    EXPECT_TRUE(geometry::approxEqual(r3, {875, 73, 2327}, 1e-9));
    EXPECT_EQ(holes(a, "sliding.2", "hw.rollers.sliding.2").size(), 4u);
    // Recessed grip (handle Center): one pocket on the visible face, no handle holes.
    EXPECT_EQ(opsFrom(part(a, "sliding.1"), "cab/rule.sliding_grip"), 1u);
    EXPECT_EQ(instance(a, "hw.handle.sliding.1"), nullptr);
    // The grip pocket: 40 x 200 x 8 with the generic 6 mm corner radius (min(6, 40 / 2) = 6).
    for (const auto& op : part(a, "sliding.1").operations) {
        if (op.source != "cab/rule.sliding_grip") continue;
        const auto& pk = std::get<domain::Pocket>(op.shape);
        EXPECT_EQ(pk.face, domain::Face::B);
        EXPECT_DOUBLE_EQ(pk.depth, 8.0);
        EXPECT_DOUBLE_EQ(pk.cornerRadius, 6.0);
        ASSERT_EQ(pk.contour.size(), 4u);
        double u0 = 1e9, u1 = -1e9, v0 = 1e9, v1 = -1e9;
        for (const auto& c : pk.contour) {
            u0 = std::min(u0, c.x); u1 = std::max(u1, c.x);
            v0 = std::min(v0, c.y); v1 = std::max(v1, c.y);
        }
        EXPECT_NEAR(std::min(u1 - u0, v1 - v0), 40, 1e-9);
        EXPECT_NEAR(std::max(u1 - u0, v1 - v0), 200, 1e-9);
        // Counter-clockwise in face coordinates: shoelace 2A = +2 * 40 * 200 = +16000.
        double twiceArea = 0;
        for (std::size_t i = 0; i < 4; ++i) {
            const auto& p0 = pk.contour[i];
            const auto& p1 = pk.contour[(i + 1) % 4];
            twiceArea += p0.x * p1.y - p1.x * p0.y;
        }
        EXPECT_NEAR(twiceArea, 16000, 1e-6);
    }
    // Interior behind the tracks: 10 + 80 + 5 = 95; partition 600 - 95 = 505 deep, shelves too.
    expectBox(part(a, "partition.1"), {891, 95, 18}, {909, 600, 2382});
    EXPECT_NEAR(part(a, "shelf.1.1").bounds().min.y, 95, 1e-9);
    EXPECT_NEAR(part(a, "shelf.1.1").width, 505, 1e-9);
}

// 3 doors: width (1764 + 2 * 30) / 3 = 608, x0 = 18 + i * 578 -> 18, 596, 1174; lanes back, front, back.
TEST(SlidingDoors, ThreeDoorWardrobeAlternatesLanes) {
    const GenerationResult r = gen(wardrobe(3));
    const domain::Assembly& a = r.assembly;
    EXPECT_THAT(errorsOf(r.diagnostics), IsEmpty());
    EXPECT_THAT(interpenetrations(a), IsEmpty());
    const double x0[] = {18, 596, 1174}, y0[] = {55, 20, 55};
    for (int i = 0; i < 3; ++i) {
        const domain::Part p = part(a, "sliding." + std::to_string(i + 1));
        EXPECT_NEAR(p.width, 608, 1e-9);
        EXPECT_NEAR(p.bounds().min.x, x0[i], 1e-9);
        EXPECT_NEAR(p.bounds().min.y, y0[i], 1e-9);
        EXPECT_EQ(holes(a, "sliding." + std::to_string(i + 1), "hw.rollers.sliding." + std::to_string(i + 1)).size(), 4u);
    }
    EXPECT_EQ(holes(a, "top", "hw.track.top").size(), 7u);
}

TEST(SlidingDoors, CountExpressionAndNoHandle) {
    CabinetDefinition d = wardrobe(2);
    d.interior.front->sliding.countExpression = "if(width > 1500, 3, 2)";
    d.interior.front->handle = HandlePosition::None;
    const GenerationResult r = gen(d);
    EXPECT_TRUE(has(r.assembly, "sliding.3"));
    EXPECT_EQ(opsFrom(part(r.assembly, "sliding.1"), "cab/rule.sliding_grip"), 0u);
    d.interior.front->sliding.countExpression = "width / 1000"; // 1.8
    auto bad = generate(d, libs());
    ASSERT_FALSE(bad.ok());
    EXPECT_THAT(bad.error().message, HasSubstr("not a whole number"));
}

TEST(SlidingDoors, Errors) {
    // 600 deep is fine; 300 leaves 300 - 95 = 205 < 250 behind the tracks.
    auto r = generate(wardrobe(2, 300), libs());
    ASSERT_FALSE(r.ok());
    EXPECT_THAT(r.error().message, HasSubstr("too shallow"));
    EXPECT_THAT(r.error().message, HasSubstr("205"));
    // 6 doors: (1764 + 150) / 6 = 319 < 400.
    r = generate(wardrobe(6), libs());
    ASSERT_FALSE(r.ok());
    EXPECT_THAT(r.error().message, HasSubstr("too narrow"));
    // Count outside 2..6.
    r = generate(wardrobe(7), libs());
    ASSERT_FALSE(r.ok());
    EXPECT_THAT(r.error().message, HasSubstr("use 2 to 6"));
    // Stretcher rails instead of a top panel cannot carry the top track.
    ConstructionOptions o;
    o.top = TopKind::Rails;
    CabinetDefinition rails = wardrobe(2);
    const auto edit = setConstruction(rails, libs(), o); // atomic: refused, definition unchanged
    ASSERT_FALSE(edit.ok());
    EXPECT_THAT(edit.error().message, HasSubstr("full top panel"));
    EXPECT_EQ(rails.construction.top, TopKind::Panel);
}

} // namespace fos::furniture::test
