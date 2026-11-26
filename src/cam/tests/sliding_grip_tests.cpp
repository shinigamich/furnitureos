// SPDX-License-Identifier: AGPL-3.0-only
// End to end: the furniture module's sliding-door recessed grip (rule.sliding_grip, a 40 x 200 x 8
// pocket on face B with the generic 6 mm Pocket::cornerRadius) is milled and simulated with the end
// mills of the fictional `fos cam` demo tool library. Compiled only when the furniture module is
// configured (see CMakeLists.txt).
#include "cam_test_fixtures.hpp"

#include "fos/cam/generate.hpp"
#include "fos/cam/pocket.hpp"
#include "fos/cam/simulate.hpp"
#include "fos/furniture/generate.hpp"
#include "fos/furniture/parameters.hpp"
#include "fos/hardware/catalog.hpp"

#include <gtest/gtest.h>

using namespace fos;
using namespace fos::cam;

namespace {

/// The end mills of fos::cli::demoToolLibrary() (fictional, src/cli/include/fos/cli/demo_cam.hpp),
/// replicated because cam must not depend on cli: T11 4/22 (DOC 3), T12 6/22 (DOC 3), T13 8/25 (DOC 4).
ToolLibrary demoEndMills() {
    ToolLibrary lib;
    for (auto t : {test::endMill("demo.endmill.4", 11, 4, 22, 3), test::endMill("demo.endmill.6", 12, 6, 22, 3),
                   test::endMill("demo.endmill.8", 13, 8, 25, 4)})
        if (!lib.add(t)) throw std::logic_error("demo tool rejected");
    return lib;
}

/// Door "sliding.1" of an 1800 x 2400 x 600 two-door wardrobe (handle Center): 2324 x 897 x 18.
domain::Part slidingDoor() {
    const domain::Model m = domain::fixtures::basicCabinet();
    furniture::Libraries libs;
    libs.materials = m.materials;
    libs.edgeBands = m.edgeBands;
    libs.hardware = hardware::builtinCatalog();
    auto d = furniture::makeCabinet(domain::AssemblyId("cab"), "Wardrobe").value();
    d.materials.carcass = domain::MaterialId("mat.pb18");
    d.materials.back = domain::MaterialId("mat.hdf3");
    d.materials.visibleEdge = domain::EdgeBandId("eb.abs2");
    d.materials.hiddenEdge = domain::EdgeBandId("eb.abs04");
    parametric::Transaction tx;
    tx.setValue("width", 1800).setValue("height", 2400).setValue("depth", 600);
    EXPECT_TRUE(d.parameters.apply(tx).ok());
    furniture::FrontSpec f;
    f.kind = furniture::FrontKind::SlidingDoors;
    f.sliding.count = 2;
    d.interior.front = f;
    auto r = furniture::generate(d, libs);
    if (!r) throw std::logic_error(r.error().message);
    for (const auto& p : r.value().assembly.parts)
        if (p.id.str() == "cab/sliding.1") return p;
    throw std::logic_error("no sliding.1");
}

} // namespace

// Grip 40 x 200, R = 6: largest end mill with d <= min(2 x 6, 40) = 12 and flute >= 8 is T13 (8 mm).
// No option radius is needed (the model's radius wins) and no rounded-corner warning is issued.
TEST(CamSlidingGrip, DemoToolsMillAndSimulateTheGrip) {
    const domain::Part door = slidingDoor();
    const cam::Setup setup{domain::Face::B, {20, 20, 0}};
    auto r = generateProgram(door, setup, test::testProfile(), demoEndMills(), CamOptions{});
    ASSERT_TRUE(r) << r.error().message;
    std::size_t grips = 0;
    for (const auto& p : r.value().plan.machinableNow) {
        const auto& op = door.operations[p.index];
        if (op.source != "cab/rule.sliding_grip") continue;
        ++grips;
        auto frame = makeSetupFrame(door, setup);
        ASSERT_TRUE(frame);
        const PocketTarget t = pocketTarget(door, frame.value(), op, 0);
        EXPECT_DOUBLE_EQ(t.cornerRadius, 6.0);
        EXPECT_TRUE(t.radiusFromModel);
    }
    EXPECT_EQ(grips, 1u);
    EXPECT_EQ(r.value().toolsUsed, std::vector<int>{13});
    for (const auto& d : r.value().diagnostics) EXPECT_NE(d.code, "cam.pocket.rounded_corner") << d.message;
    const auto sim = simulate(r.value().program, door, setup, test::testProfile(), demoEndMills(), SimulationOptions{});
    std::string dump;
    for (const auto& d : sim.diagnostics) dump += d.code + ": " + d.message + "\n";
    EXPECT_FALSE(sim.hasErrors()) << dump;
    EXPECT_GT(sim.cellsRemoved, 0u);
}

// Only end mills larger than 12 mm: refused, naming the 12 mm limit (2 x the 6 mm corner radius).
TEST(CamSlidingGrip, RefusesWhenNoDemoEndMillFitsTheCornerRadius) {
    const domain::Part door = slidingDoor();
    ToolLibrary lib;
    ASSERT_TRUE(lib.add(test::endMill("em.16", 5, 16, 30, 4)));
    auto r = generateProgram(door, {domain::Face::B, {20, 20, 0}}, test::testProfile(), lib, CamOptions{});
    ASSERT_FALSE(r);
    EXPECT_NE(r.error().message.find("diameter <= 12 mm"), std::string::npos) << r.error().message;
    EXPECT_NE(r.error().message.find("corner radius 6 mm"), std::string::npos) << r.error().message;
}
